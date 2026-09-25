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
 * XWayland server lifecycle management.
 */

#ifndef SRC_XWAYLAND_XWAYLAND_MANAGER_XWAYLAND_MANAGER_H_
#define SRC_XWAYLAND_XWAYLAND_MANAGER_XWAYLAND_MANAGER_H_

#include <list>
#include <memory>

#include "src/core/compositor_private/compositor_private.h"
#include "src/utils/signal_listener.h"
#include "src/wlr_wrapper/wlroots.h"

namespace flakewm {
namespace xwayland {

class XWaylandManager final {
 public:
  explicit XWaylandManager(core::CompositorPrivate* compositor);
  ~XWaylandManager();

  XWaylandManager(const XWaylandManager&) = delete;
  XWaylandManager& operator=(const XWaylandManager&) = delete;

  bool Start(wl_display* display, wlr_compositor* compositor);
  void Stop();
  const char* DisplayName() const;

  // The XWM only serves the clipboard while some X toplevel holds its focus.
  // Whenever the keyboard is on a Wayland surface, that focus is parked on a
  // hidden input-only window instead, so X clients keep clipboard access
  // while still seeing themselves as unfocused.
  void Activate(wlr_xwayland_surface* surface);
  void Deactivate(wlr_xwayland_surface* surface);
  void Park();

  int ToX(int logical) const;
  int FromX(int x) const;
  bool OwnsSurface(const wlr_surface* surface) const;
  bool OwnsClient(const wl_client* client) const;
  void ToSurfaceX(const wlr_surface* surface, double* x, double* y) const;

 private:
  struct ScaledSurface;
  struct ClientWatch {
    wl_listener resource_created;
    wl_listener destroy;
    XWaylandManager* manager;
    wl_client* client;
  };

  static void OnReady(XWaylandManager* manager, void*);
  static void OnNewSurface(XWaylandManager* manager,
                           wlr_xwayland_surface* surface);
  static void OnParkDestroy(XWaylandManager* manager, void*);
  static void OnNewWlSurface(XWaylandManager* manager, wlr_surface* surface);
  static void OnLayoutChange(XWaylandManager* manager, void*);
  static void OnResourceCreated(wl_listener* listener, void* data);
  static void OnClientDestroy(wl_listener* listener, void* data);
  static void OnSendOutputsIdle(void* data);
  static bool OnXEvent(wlr_xwayland* xwayland, xcb_generic_event_t* event);

  wl_client* Client() const;
  void UpdateScale();
  void SendOutputs();
  void PublishResources();
  void SetDefaultCursor();
  void WatchClient();
  void UnwatchClient();

  core::CompositorPrivate* compositor;
  wlr_xwayland* handle = nullptr;
  double scale = 1.0;
  std::list<std::unique_ptr<ScaledSurface>> scaled_surfaces;
  ClientWatch client_watch = {};
  wl_event_source* send_outputs_idle = nullptr;
  int cursor_size_base = 0;
  int cursor_size_written = 0;
  // X toplevel holding the XWM focus, or null while parked.
  wlr_xwayland_surface* focused = nullptr;
  xcb_window_t park_window = XCB_WINDOW_NONE;
  wlr_xwayland_surface* park_surface = nullptr;
  utils::SignalListener<XWaylandManager, void> ready{this, OnReady};
  utils::SignalListener<XWaylandManager, wlr_xwayland_surface> new_surface{
      this, OnNewSurface};
  utils::SignalListener<XWaylandManager, void> park_destroy{this,
                                                            OnParkDestroy};
  utils::SignalListener<XWaylandManager, wlr_surface> new_wl_surface{
      this, OnNewWlSurface};
  utils::SignalListener<XWaylandManager, void> layout_change{this,
                                                             OnLayoutChange};
};

}  // namespace xwayland
}  // namespace flakewm

#endif  // SRC_XWAYLAND_XWAYLAND_MANAGER_XWAYLAND_MANAGER_H_
