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

 private:
  static void OnReady(XWaylandManager* manager, void*);
  static void OnNewSurface(XWaylandManager* manager,
                           wlr_xwayland_surface* surface);

  core::CompositorPrivate* compositor;
  wlr_xwayland* handle = nullptr;
  utils::SignalListener<XWaylandManager, void> ready{this, OnReady};
  utils::SignalListener<XWaylandManager, wlr_xwayland_surface> new_surface{
      this, OnNewSurface};
};

}  // namespace xwayland
}  // namespace flakewm

#endif  // SRC_XWAYLAND_XWAYLAND_MANAGER_XWAYLAND_MANAGER_H_
