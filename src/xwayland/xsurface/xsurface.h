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

#ifndef SRC_XWAYLAND_XSURFACE_XSURFACE_H_
#define SRC_XWAYLAND_XSURFACE_XSURFACE_H_

#include "src/core/compositor_private/compositor_private.h"

namespace flakewm {
namespace xwayland {

class XSurface final : public core::CompositorPrivate::Toplevel {
 public:
  XSurface(core::CompositorPrivate* compositor, wlr_xwayland_surface* surface);
  ~XSurface() override;

  XSurface(const XSurface&) = delete;
  XSurface& operator=(const XSurface&) = delete;

 private:
  bool IsAlive() const override;
  bool IsXWayland() const override;
  bool WantsFocus() const override;
  bool CanManage() const override;
  bool CanMinimize() const override;
  bool CanMaximize() const override;
  bool RequestedMaximized() const override;
  bool RequestedFullscreen() const override;
  const char* Title() const override;
  const char* AppId() const override;
  wlr_surface* Surface() const override;
  wlr_box Geometry() const override;
  void Configure(const wlr_box& box) const override;
  void SetActivated(bool activated) const override;
  void SetMaximizedState(bool maximized) const override;
  void SetMinimizedState(bool minimized) const override;
  void SetFullscreenState(bool fullscreen) const override;
  void Restack() const override;
  void Close() const override;

  static void OnAssociate(XSurface* surface, void*);
  static void OnDissociate(XSurface* surface, void*);
  static void OnRequestConfigure(XSurface* surface,
                                 wlr_xwayland_surface_configure_event* event);
  static void OnRequestResize(XSurface* surface,
                              wlr_xwayland_resize_event* event);
  static void OnRequestMinimize(XSurface* surface,
                                wlr_xwayland_minimize_event* event);
  static void OnRequestActivate(XSurface* surface, void*);
  static void OnSetGeometry(XSurface* surface, void*);
  static void OnDestroy(XSurface* surface, void*);

  void Dissociate();

  wlr_xwayland_surface* handle;
  utils::SignalListener<XSurface, void> associate{this, OnAssociate};
  utils::SignalListener<XSurface, void> dissociate{this, OnDissociate};
  utils::SignalListener<XSurface, wlr_xwayland_surface_configure_event>
      request_configure{this, OnRequestConfigure};
  utils::SignalListener<XSurface, wlr_xwayland_resize_event> request_resize{
      this, OnRequestResize};
  utils::SignalListener<XSurface, wlr_xwayland_minimize_event> request_minimize{
      this, OnRequestMinimize};
  utils::SignalListener<XSurface, void> request_activate{this,
                                                         OnRequestActivate};
  utils::SignalListener<XSurface, void> set_geometry{this, OnSetGeometry};
  utils::SignalListener<XSurface, void> destroy_xsurface{this, OnDestroy};
};

}  // namespace xwayland
}  // namespace flakewm

#endif  // SRC_XWAYLAND_XSURFACE_XSURFACE_H_
