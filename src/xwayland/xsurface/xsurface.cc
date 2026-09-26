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
 * X11 window lifecycle management.
 */

#include "src/xwayland/xsurface/xsurface.h"

#include <absl/log/absl_log.h>

#include <algorithm>
#include <cstdint>
#include <limits>

#include "src/xwayland/xwayland_manager/xwayland_manager.h"

namespace flakewm {
namespace xwayland {

XSurface::XSurface(core::CompositorPrivate* compositor,
                   wlr_xwayland_surface* surface)
    : Toplevel(compositor), handle(surface) {
  associate.Connect(&handle->events.associate);
  dissociate.Connect(&handle->events.dissociate);
  destroy_xsurface.Connect(&handle->events.destroy);
  request_configure.Connect(&handle->events.request_configure);
  request_move.Connect(&handle->events.request_move);
  request_resize.Connect(&handle->events.request_resize);
  request_minimize.Connect(&handle->events.request_minimize);
  request_maximize.Connect(&handle->events.request_maximize);
  request_fullscreen.Connect(&handle->events.request_fullscreen);
  request_activate.Connect(&handle->events.request_activate);
  set_geometry.Connect(&handle->events.set_geometry);

  if (handle->surface != nullptr) {
    OnAssociate(this, nullptr);
  }
}

XSurface::~XSurface() { Dissociate(); }

bool XSurface::IsAlive() const { return handle != nullptr; }

bool XSurface::IsXWayland() const { return true; }

bool XSurface::WantsFocus() const {
  if (handle == nullptr || !handle->override_redirect) {
    return handle != nullptr;
  }

  return wlr_xwayland_surface_override_redirect_wants_focus(handle);
}

bool XSurface::CanManage() const {
  return handle != nullptr && !handle->override_redirect;
}

bool XSurface::CanMinimize() const {
  return CanManage() && handle->parent == nullptr && !handle->modal;
}

bool XSurface::CanMaximize() const {
  return CanManage() && handle->parent == nullptr && !handle->modal;
}

bool XSurface::RequestedMaximized() const {
  return handle != nullptr && handle->maximized_horz && handle->maximized_vert;
}

bool XSurface::RequestedFullscreen() const {
  return handle != nullptr && handle->fullscreen;
}

const char* XSurface::Title() const {
  return handle == nullptr ? nullptr : handle->title;
}

const char* XSurface::AppId() const {
  return handle == nullptr ? nullptr : handle->class_;
}

wlr_surface* XSurface::Surface() const {
  return handle == nullptr ? nullptr : handle->surface;
}

wlr_box XSurface::Geometry() const {
  if (handle == nullptr) {
    return {};
  }

  const XWaylandManager* xwayland = compositor->xwayland_.get();
  return {.x = 0,
          .y = 0,
          .width = xwayland->FromX(handle->width),
          .height = xwayland->FromX(handle->height)};
}

void XSurface::Configure(const wlr_box& box) const {
  if (handle == nullptr || scene_tree == nullptr || box.width <= 0 ||
      box.height <= 0) {
    return;
  }

  wlr_scene_node_set_position(&scene_tree->node, box.x, box.y);

  const XWaylandManager* xwayland = compositor->xwayland_.get();
  const int x = xwayland->ToX(box.x);
  const int y = xwayland->ToX(box.y);
  const int width = xwayland->ToX(box.width);
  const int height = xwayland->ToX(box.height);
  // X11 geometry uses 16-bit.
  constexpr int kMinCoordinate = std::numeric_limits<int16_t>::min();
  constexpr int kMaxCoordinate = std::numeric_limits<int16_t>::max();
  constexpr int kMaxDimension = std::numeric_limits<uint16_t>::max();
  wlr_xwayland_surface_configure(
      handle,
      static_cast<int16_t>(std::clamp(x, kMinCoordinate, kMaxCoordinate)),
      static_cast<int16_t>(std::clamp(y, kMinCoordinate, kMaxCoordinate)),
      static_cast<uint16_t>(std::clamp(width, 1, kMaxDimension)),
      static_cast<uint16_t>(std::clamp(height, 1, kMaxDimension)));
}

void XSurface::SetActivated(bool activated) const {
  if (handle == nullptr || compositor->xwayland_ == nullptr) {
    return;
  }
  if (activated) {
    compositor->xwayland_->Activate(handle);
  } else {
    compositor->xwayland_->Deactivate(handle);
  }
}

void XSurface::SetMaximizedState(bool maximized) const {
  if (handle != nullptr) {
    wlr_xwayland_surface_set_maximized(handle, maximized, maximized);
  }
}

void XSurface::SetMinimizedState(bool minimized) const {
  if (handle != nullptr) {
    wlr_xwayland_surface_set_minimized(handle, minimized);
  }
}

void XSurface::SetFullscreenState(bool fullscreen) const {
  if (handle != nullptr) {
    wlr_xwayland_surface_set_fullscreen(handle, fullscreen);
  }
}

void XSurface::Restack() const {
  if (CanManage()) {
    wlr_xwayland_surface_restack(handle, nullptr, XCB_STACK_MODE_ABOVE);
  }
}

void XSurface::Close() const {
  if (handle != nullptr) {
    wlr_xwayland_surface_close(handle);
  }
}

void XSurface::OnAssociate(XSurface* surface, void*) {
  if (surface->handle == nullptr || surface->handle->surface == nullptr ||
      surface->scene_tree != nullptr) {
    return;
  }

  // The X window gains its wl_surface here. Should be a toplevel.
  surface->scene_tree = wlr_scene_subsurface_tree_create(
      surface->compositor->toplevel_tree_, surface->handle->surface);
  if (surface->scene_tree == nullptr) {
    ABSL_LOG(ERROR) << "Failed to create scene tree for XWayland surface.";
    return;
  }
  surface->scene_tree->node.data = surface;
  const XWaylandManager* xwayland = surface->compositor->xwayland_.get();
  wlr_scene_node_set_position(&surface->scene_tree->node,
                              xwayland->FromX(surface->handle->x),
                              xwayland->FromX(surface->handle->y));
  surface->compositor->RebuildSurfaceClip(surface);
  surface->map.Connect(&surface->handle->surface->events.map);
  surface->unmap.Connect(&surface->handle->surface->events.unmap);
  surface->commit.Connect(&surface->handle->surface->events.commit);

  if (surface->handle->surface->mapped) {
    Toplevel::OnMap(surface, nullptr);
  }
}

void XSurface::Dissociate() {
  if (handle != nullptr && handle->surface != nullptr) {
    compositor->ClearSurfaceRoundCorner(handle->surface);
  }
  if (mapped) {
    Toplevel::OnUnmap(this, nullptr);
  }
  map.Disconnect();
  unmap.Disconnect();
  commit.Disconnect();
  if (scene_tree != nullptr) {
    wlr_scene_node_destroy(&scene_tree->node);
    scene_tree = nullptr;
  }
  mapped = false;
}

void XSurface::OnDissociate(XSurface* surface, void*) {
  // Stop watching the wl_surface before cleanup.
  surface->Dissociate();
}

void XSurface::OnRequestConfigure(XSurface* surface,
                                  wlr_xwayland_surface_configure_event* event) {
  if (surface->handle == nullptr || event == nullptr) {
    return;
  }

  // Managed maximized windows shall keep WM geometry, while other X11 windows
  // could keep their root-window coordinates.
  if (surface->maximized && surface->CanManage()) {
    surface->Configure(surface->maximized_box);
    return;
  }

  const XWaylandManager* xwayland = surface->compositor->xwayland_.get();
  surface->Configure({.x = xwayland->FromX(event->x),
                      .y = xwayland->FromX(event->y),
                      .width = xwayland->FromX(event->width),
                      .height = xwayland->FromX(event->height)});
}

void XSurface::OnRequestResize(XSurface* surface,
                               wlr_xwayland_resize_event* event) {
  if (event != nullptr) {
    surface->compositor->BeginInteractive(
        surface, core::CompositorPrivate::CursorMode::kResize, event->edges);
  }
}

void XSurface::OnRequestMinimize(XSurface* surface,
                                 wlr_xwayland_minimize_event* event) {
  if (event == nullptr) {
    return;
  }
  if (event->minimize) {
    surface->compositor->Minimize(surface);
    return;
  }

  surface->minimized = false;
  surface->SetMinimizedState(false);
  if (surface->scene_tree != nullptr) {
    wlr_scene_node_set_enabled(&surface->scene_tree->node, true);
  }
  if (surface->mapped && surface->WantsFocus()) {
    surface->compositor->FocusToplevel(surface);
  }
}

void XSurface::OnRequestActivate(XSurface* surface, void*) {
  if (surface->WantsFocus()) {
    ABSL_LOG(INFO) << "X11 window " << surface->handle->window_id
                   << " activated.";
    surface->compositor->FocusToplevel(surface);
  }
}

void XSurface::OnSetGeometry(XSurface* surface, void*) {
  if (surface->handle == nullptr || surface->scene_tree == nullptr ||
      surface->maximized || surface->compositor->grabbed_toplevel_ == surface) {
    return;
  }

  const XWaylandManager* xwayland = surface->compositor->xwayland_.get();
  wlr_scene_node_set_position(&surface->scene_tree->node,
                              xwayland->FromX(surface->handle->x),
                              xwayland->FromX(surface->handle->y));
}

void XSurface::OnDestroy(XSurface* surface, void*) {
  surface->associate.Disconnect();
  surface->dissociate.Disconnect();
  surface->request_configure.Disconnect();
  surface->request_resize.Disconnect();
  surface->request_minimize.Disconnect();
  surface->request_activate.Disconnect();
  surface->set_geometry.Disconnect();
  surface->destroy_xsurface.Disconnect();
  surface->Dissociate();
  surface->SetActivated(false);
  surface->handle = nullptr;
  Toplevel::OnDestroy(surface, nullptr);
}

}  // namespace xwayland
}  // namespace flakewm
