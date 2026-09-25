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

#include <array>
#include <climits>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>

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

}  // namespace

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
    ready.Connect(&handle->events.ready);
    new_surface.Connect(&handle->events.new_surface);

    if (wl_event_loop_dispatch(wl_display_get_event_loop(display), 0) < 0) {
      Stop();
    }
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
  focused = nullptr;
  park_surface = nullptr;
  park_window = XCB_WINDOW_NONE;
  if (handle == nullptr) {
    return;
  }

  // This also stops the child process and XWM.
  wlr_xwayland_destroy(handle);
  handle = nullptr;
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
  wlr_xcursor* cursor = wlr_xcursor_manager_get_xcursor(
      manager->compositor->cursor_manager_, "default", 1.0F);
  if (cursor != nullptr && cursor->image_count > 0) {
    wlr_xcursor_image* image = cursor->images[0];
    wlr_xwayland_set_cursor(manager->handle,
                            wlr_xcursor_image_get_buffer(image),
                            image->hotspot_x, image->hotspot_y);
  }

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
