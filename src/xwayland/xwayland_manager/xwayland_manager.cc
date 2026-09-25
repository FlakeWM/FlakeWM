/*
 * Copyright (C) 2026 CharOfString <root@charofstring.cc>
 *
 * This file is part of FLAKEWM.
 *
 * FLAKEWM is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * FLAKEWM is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * FLAKEWM. If not, see <https://www.gnu.org/licenses/>.
 * ----------------------------------------------------------------------------
 * XWayland server lifecycle.
 */

#include "src/xwayland/xwayland_manager/xwayland_manager.h"

#include <absl/log/absl_log.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "src/core/compositor_private/compositor_private.h"
#include "src/xwayland/xsurface/xsurface.h"

namespace flakewm {
namespace xwayland {
namespace {

std::string LauncherPath() {
  std::array<char, PATH_MAX> executable{};
  const ssize_t length =
      readlink("/proc/self/exe", executable.data(), executable.size() - 1);
  if (length <= 0) {
    return {};
  }

  std::string path(executable.data(), static_cast<size_t>(length));
  const size_t separator = path.find_last_of('/');
  if (separator == std::string::npos) {
    return {};
  }
  path.replace(separator + 1, std::string::npos, "flakewm-xwayland");
  return access(path.c_str(), X_OK) == 0 ? path : std::string{};
}

// zxdg_output_v1 event opcodes
constexpr uint32_t kXdgOutputLogicalPosition = 0;
constexpr uint32_t kXdgOutputLogicalSize = 1;
constexpr uint32_t kXdgOutputDone = 2;
constexpr int kXdgOutputDoneDeprecatedSince = 3;

// Replaces or appends "key:\tvalue" in an X resource database
void SetResource(std::string* resources, std::string_view key,
                 const std::string& value) {
  const std::string line = std::string(key) + ":\t" + value;
  size_t start = 0;
  while (start < resources->size()) {
    size_t end = resources->find('\n', start);
    if (end == std::string::npos) {
      end = resources->size();
    }
    const std::string_view entry(resources->data() + start, end - start);
    if (entry.substr(0, key.size()) == key && entry.size() > key.size() &&
        entry[key.size()] == ':') {
      resources->replace(start, end - start, line);
      return;
    }
    start = end + 1;
  }
  if (!resources->empty() && resources->back() != '\n') {
    resources->push_back('\n');
  }
  *resources += line + '\n';
}

int GetResourceInt(const std::string& resources, std::string_view key) {
  size_t start = 0;
  while (start < resources.size()) {
    size_t end = resources.find('\n', start);
    if (end == std::string::npos) {
      end = resources.size();
    }
    const std::string_view entry(resources.data() + start, end - start);
    if (entry.substr(0, key.size()) == key && entry.size() > key.size() &&
        entry[key.size()] == ':') {
      return std::atoi(std::string(entry.substr(key.size() + 1)).c_str());
    }
    start = end + 1;
  }
  return 0;
}

}  // namespace

// Shrinks each commit of an Xwayland surface from X pixels to layout units
// before it is applied
struct XWaylandManager::ScaledSurface {
  ScaledSurface(XWaylandManager* manager, wlr_surface* surface)
      : manager(manager), surface(surface) {
    client_commit.Connect(&surface->events.client_commit);
    destroy.Connect(&surface->events.destroy);
  }

  static void OnClientCommit(ScaledSurface* self, void*) {
    const double scale = self->manager->scale;
    if (scale == 1.0) {
      return;
    }
    wlr_surface_state* pending = &self->surface->pending;
    pending->width = static_cast<int>(std::lround(pending->width / scale));
    pending->height = static_cast<int>(std::lround(pending->height / scale));
    // Surface damage stays in X pixels: with buffer scale 1 and no viewport
    // wlroots maps it 1:1 onto the buffer.
    if ((pending->committed & WLR_SURFACE_STATE_INPUT_REGION) != 0) {
      wlr_region_scale(&pending->input, &pending->input,
                       static_cast<float>(1.0 / scale));
    }
    if ((pending->committed & WLR_SURFACE_STATE_OPAQUE_REGION) != 0) {
      wlr_region_scale(&pending->opaque, &pending->opaque,
                       static_cast<float>(1.0 / scale));
    }
    if ((pending->committed & WLR_SURFACE_STATE_OFFSET) != 0) {
      pending->dx = static_cast<int32_t>(std::lround(pending->dx / scale));
      pending->dy = static_cast<int32_t>(std::lround(pending->dy / scale));
    }
  }

  static void OnDestroy(ScaledSurface* self, void*) {
    auto& list = self->manager->scaled_surfaces;
    list.remove_if([self](const std::unique_ptr<ScaledSurface>& entry) {
      return entry.get() == self;
    });
  }

  XWaylandManager* manager;
  wlr_surface* surface;
  utils::SignalListener<ScaledSurface, void> client_commit{this,
                                                           OnClientCommit};
  utils::SignalListener<ScaledSurface, void> destroy{this, OnDestroy};
};

XWaylandManager::XWaylandManager(core::CompositorPrivate* compositor)
    : compositor(compositor) {}

XWaylandManager::~XWaylandManager() { Stop(); }

bool XWaylandManager::Start(wl_display* display,
                            wlr_compositor* wlr_compositor) {
  if (handle != nullptr || display == nullptr || wlr_compositor == nullptr) {
    return false;
  }

  const bool has_launcher = std::getenv("WLR_XWAYLAND") != nullptr;
  const std::string launcher = has_launcher ? "" : LauncherPath();
  const bool use_helper = !launcher.empty();
  if (use_helper) {
    setenv("WLR_XWAYLAND", launcher.c_str(), 1);
  }

  handle = wlr_xwayland_create(display, wlr_compositor, false);
  if (handle != nullptr) {
    handle->data = this;
    handle->user_event_handler = OnXEvent;
    ready.Connect(&handle->events.ready);
    new_surface.Connect(&handle->events.new_surface);
    new_wl_surface.Connect(&wlr_compositor->events.new_surface);
    layout_change.Connect(&compositor->output_layout_->events.change);
    UpdateScale();

    if (wl_event_loop_dispatch(wl_display_get_event_loop(display), 0) < 0) {
      Stop();
    }
    WatchClient();
  }
  if (use_helper) {
    unsetenv("WLR_XWAYLAND");
  }
  if (handle == nullptr) {
    return false;
  }
  return true;
}

void XWaylandManager::Stop() {
  park_destroy.Disconnect();
  new_surface.Disconnect();
  ready.Disconnect();
  new_wl_surface.Disconnect();
  layout_change.Disconnect();
  scaled_surfaces.clear();
  UnwatchClient();
  if (send_outputs_idle != nullptr) {
    wl_event_source_remove(send_outputs_idle);
    send_outputs_idle = nullptr;
  }
  focused = nullptr;
  park_surface = nullptr;
  park_window = XCB_WINDOW_NONE;
  if (handle == nullptr) {
    return;
  }

  // This also stops the child process and XWM.
  handle->user_event_handler = nullptr;
  wlr_xwayland_destroy(handle);
  handle = nullptr;
}

int XWaylandManager::ToX(int logical) const {
  return static_cast<int>(std::lround(logical * scale));
}

int XWaylandManager::FromX(int x) const {
  return static_cast<int>(std::lround(x / scale));
}

wl_client* XWaylandManager::Client() const {
  return handle == nullptr || handle->server == nullptr
             ? nullptr
             : handle->server->client;
}

bool XWaylandManager::OwnsClient(const wl_client* client) const {
  return client != nullptr && client == Client();
}

bool XWaylandManager::OwnsSurface(const wlr_surface* surface) const {
  return surface != nullptr && surface->resource != nullptr &&
         OwnsClient(wl_resource_get_client(surface->resource));
}

void XWaylandManager::ToSurfaceX(const wlr_surface* surface, double* x,
                                 double* y) const {
  if (OwnsSurface(surface)) {
    *x *= scale;
    *y *= scale;
  }
}

void XWaylandManager::UpdateScale() {
  double largest = 1.0;
  wlr_output_layout_output* layout_output = nullptr;
  wl_list_for_each(layout_output, &compositor->output_layout_->outputs, link) {
    largest =
        std::max(largest, static_cast<double>(layout_output->output->scale));
  }
  if (largest == scale) {
    return;
  }

  // Keep managed windows where they are in layout units
  std::vector<std::pair<core::CompositorPrivate::Toplevel*, wlr_box>> boxes;
  for (const auto& toplevel : compositor->toplevels_) {
    if (toplevel->IsXWayland() && toplevel->mapped && toplevel->CanManage() &&
        toplevel->scene_tree != nullptr) {
      const wlr_box geometry = toplevel->Geometry();
      boxes.emplace_back(toplevel.get(),
                         wlr_box{.x = toplevel->scene_tree->node.x,
                                 .y = toplevel->scene_tree->node.y,
                                 .width = geometry.width,
                                 .height = geometry.height});
    }
  }
  scale = largest;
  ABSL_LOG(INFO) << "XWayland scale is now " << scale << '.';
  for (const auto& [toplevel, box] : boxes) {
    toplevel->Configure(box);
  }
  PublishResources();
  SetDefaultCursor();
}

void XWaylandManager::SendOutputs() {
  wl_client* client = Client();
  wlr_xdg_output_manager_v1* manager = compositor->xdg_output_manager_;
  if (client == nullptr || manager == nullptr) {
    return;
  }
  wlr_xdg_output_v1* output = nullptr;
  wl_list_for_each(output, &manager->outputs, link) {
    wl_resource* resource = nullptr;
    wl_resource_for_each(resource, &output->resources) {
      if (wl_resource_get_client(resource) != client) {
        continue;
      }
      wl_resource_post_event(resource, kXdgOutputLogicalPosition,
                             ToX(output->x), ToX(output->y));
      wl_resource_post_event(resource, kXdgOutputLogicalSize,
                             ToX(output->width), ToX(output->height));
      if (wl_resource_get_version(resource) < kXdgOutputDoneDeprecatedSince) {
        wl_resource_post_event(resource, kXdgOutputDone);
        continue;
      }
      wl_resource* output_resource = nullptr;
      wl_resource_for_each(output_resource,
                           &output->layout_output->output->resources) {
        if (wl_resource_get_client(output_resource) == client &&
            wl_resource_get_version(output_resource) >=
                WL_OUTPUT_DONE_SINCE_VERSION) {
          wl_output_send_done(output_resource);
        }
      }
    }
  }
}

void XWaylandManager::PublishResources() {
  xcb_connection_t* connection =
      handle == nullptr ? nullptr : wlr_xwayland_get_xwm_connection(handle);
  if (connection == nullptr) {
    return;
  }
  const xcb_window_t root =
      xcb_setup_roots_iterator(xcb_get_setup(connection)).data->root;
  xcb_get_property_reply_t* reply = xcb_get_property_reply(
      connection,
      xcb_get_property(connection, 0, root, XCB_ATOM_RESOURCE_MANAGER,
                       XCB_ATOM_STRING, 0, 1U << 20),
      nullptr);
  std::string current;
  if (reply != nullptr) {
    current.assign(static_cast<const char*>(xcb_get_property_value(reply)),
                   static_cast<size_t>(xcb_get_property_value_length(reply)));
    free(reply);
  }

  // startdde reads Xft.dpi back as the X scale, and rewrites both keys from
  // its own settings
  // keep them at native resolution whoever wrote last.
  const int cursor_size = GetResourceInt(current, "Xcursor.size");
  if (cursor_size > 0 && cursor_size != cursor_size_written) {
    cursor_size_base = cursor_size;
  }
  if (cursor_size_base <= 0) {
    cursor_size_base = static_cast<int>(compositor->cursor_manager_->size);
  }
  cursor_size_written = ToX(cursor_size_base);

  std::string wanted = current;
  SetResource(&wanted, "Xft.dpi", std::to_string(ToX(96)));
  SetResource(&wanted, "Xcursor.size", std::to_string(cursor_size_written));
  if (wanted != current) {
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, root,
                        XCB_ATOM_RESOURCE_MANAGER, XCB_ATOM_STRING, 8,
                        static_cast<uint32_t>(wanted.size()), wanted.data());
    xcb_flush(connection);
  }
}

void XWaylandManager::SetDefaultCursor() {
  if (handle == nullptr || handle->xwm == nullptr) {
    return;
  }

  const auto cursor_scale = static_cast<float>(scale);
  wlr_xcursor_manager_load(compositor->cursor_manager_, cursor_scale);
  wlr_xcursor* cursor = wlr_xcursor_manager_get_xcursor(
      compositor->cursor_manager_, "default", cursor_scale);
  if (cursor != nullptr && cursor->image_count > 0) {
    wlr_xcursor_image* image = cursor->images[0];
    wlr_xwayland_set_cursor(handle, wlr_xcursor_image_get_buffer(image),
                            static_cast<int32_t>(image->hotspot_x),
                            static_cast<int32_t>(image->hotspot_y));
  }
}

void XWaylandManager::WatchClient() {
  wl_client* client = Client();
  if (client == client_watch.client) {
    return;
  }
  UnwatchClient();
  if (client == nullptr) {
    return;
  }
  client_watch.manager = this;
  client_watch.client = client;
  client_watch.resource_created.notify = OnResourceCreated;
  wl_client_add_resource_created_listener(client,
                                          &client_watch.resource_created);
  client_watch.destroy.notify = OnClientDestroy;
  wl_client_add_destroy_listener(client, &client_watch.destroy);
}

void XWaylandManager::UnwatchClient() {
  if (client_watch.client == nullptr) {
    return;
  }
  wl_list_remove(&client_watch.resource_created.link);
  wl_list_remove(&client_watch.destroy.link);
  client_watch.client = nullptr;
}

void XWaylandManager::OnResourceCreated(wl_listener* listener, void* data) {
  ClientWatch* watch = wl_container_of(listener, watch, resource_created);
  XWaylandManager* manager = watch->manager;
  auto* resource = static_cast<wl_resource*>(data);
  // wlroots answers get_xdg_output with layout units right after creating the
  // resource
  if (std::strcmp(wl_resource_get_class(resource), "zxdg_output_v1") != 0 ||
      manager->send_outputs_idle != nullptr) {
    return;
  }
  manager->send_outputs_idle = wl_event_loop_add_idle(
      wl_display_get_event_loop(manager->handle->wl_display), OnSendOutputsIdle,
      manager);
}

void XWaylandManager::OnClientDestroy(wl_listener* listener, void*) {
  ClientWatch* watch = wl_container_of(listener, watch, destroy);
  // libwayland drops the resource_created list head right after this.
  wl_list_remove(&watch->resource_created.link);
  wl_list_init(&watch->resource_created.link);
  wl_list_init(&watch->destroy.link);
  watch->client = nullptr;
}

void XWaylandManager::OnSendOutputsIdle(void* data) {
  auto* manager = static_cast<XWaylandManager*>(data);
  manager->send_outputs_idle = nullptr;
  manager->SendOutputs();
}

bool XWaylandManager::OnXEvent(wlr_xwayland* xwayland,
                               xcb_generic_event_t* event) {
  auto* manager = static_cast<XWaylandManager*>(xwayland->data);
  if (manager != nullptr &&
      (event->response_type & ~0x80) == XCB_PROPERTY_NOTIFY &&
      reinterpret_cast<xcb_property_notify_event_t*>(event)->atom ==
          XCB_ATOM_RESOURCE_MANAGER) {
    manager->PublishResources();
  }
  // Let the XWM handle it as well.
  return false;
}

void XWaylandManager::OnNewWlSurface(XWaylandManager* manager,
                                     wlr_surface* surface) {
  if (manager->OwnsSurface(surface)) {
    manager->scaled_surfaces.push_back(
        std::make_unique<ScaledSurface>(manager, surface));
  }
}

void XWaylandManager::OnLayoutChange(XWaylandManager* manager, void*) {
  manager->UpdateScale();
  manager->SendOutputs();
}

const char* XWaylandManager::DisplayName() const {
  return handle == nullptr ? "" : handle->display_name;
}

void XWaylandManager::Activate(wlr_xwayland_surface* surface) {
  if (surface == nullptr) {
    return;
  }
  if (surface->override_redirect) {
    // The XWM cannot focus these. With no X toplevel to keep, unpark so X
    // input falls back to PointerRoot and the window under the pointer gets
    // the keys, as a parked focus would swallow them.
    if (focused == nullptr && park_surface != nullptr) {
      wlr_xwayland_surface_activate(park_surface, false);
    }
    return;
  }
  focused = surface;
  wlr_xwayland_surface_activate(surface, true);
}

void XWaylandManager::Deactivate(wlr_xwayland_surface* surface) {
  if (surface != nullptr && surface == focused) {
    Park();
  }
}

void XWaylandManager::Park() {
  wlr_xwayland_surface* previous = focused;
  focused = nullptr;
  if (park_surface != nullptr) {
    wlr_xwayland_surface_activate(park_surface, true);
  } else if (previous != nullptr) {
    wlr_xwayland_surface_activate(previous, false);
  }
}

void XWaylandManager::OnReady(XWaylandManager* manager, void*) {
  if (manager->handle == nullptr) {
    return;
  }

  // The XWM exists only after ready, handle seat here instead of onstart.
  wlr_xwayland_set_seat(manager->handle, manager->compositor->seat_);
  manager->SetDefaultCursor();
  // A restarted Xwayland comes back as a new client.
  manager->WatchClient();
  manager->SendOutputs();
  manager->PublishResources();

  // Created on the XWM's own connection, so mapping it is not redirected and
  // it arrives through new_surface as a regular managed window. Input-only
  // windows get no wl_surface: nothing to draw, and the XWM leaves it out of
  // _NET_CLIENT_LIST.
  xcb_connection_t* connection =
      wlr_xwayland_get_xwm_connection(manager->handle);
  if (connection != nullptr && manager->park_window == XCB_WINDOW_NONE) {
    const xcb_screen_t* screen =
        xcb_setup_roots_iterator(xcb_get_setup(connection)).data;
    manager->park_window = xcb_generate_id(connection);
    xcb_create_window(connection, XCB_COPY_FROM_PARENT, manager->park_window,
                      screen->root, -100, -100, 1, 1, 0,
                      XCB_WINDOW_CLASS_INPUT_ONLY, XCB_COPY_FROM_PARENT, 0,
                      nullptr);
    xcb_map_window(connection, manager->park_window);
    xcb_flush(connection);
  }
  ABSL_LOG(INFO) << "XWayland server is ready on DISPLAY="
                 << manager->DisplayName() << '.';
}

void XWaylandManager::OnNewSurface(XWaylandManager* manager,
                                   wlr_xwayland_surface* surface) {
  if (surface->window_id == manager->park_window) {
    manager->park_surface = surface;
    manager->park_destroy.Connect(&surface->events.destroy);
    wlr_xwayland_surface_set_skip_taskbar(surface, true);
    wlr_xwayland_surface_set_skip_pager(surface, true);
    if (manager->focused == nullptr) {
      manager->Park();
    }
    return;
  }

  auto state = std::make_unique<XSurface>(manager->compositor, surface);
  manager->compositor->toplevels_.push_back(std::move(state));
  ABSL_LOG(INFO) << "Got new XWayland window: " << surface->window_id << '.';
}

void XWaylandManager::OnParkDestroy(XWaylandManager* manager, void*) {
  manager->park_destroy.Disconnect();
  manager->park_surface = nullptr;
}

}  // namespace xwayland
}  // namespace flakewm
