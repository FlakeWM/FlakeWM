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
 * Layer shell surface state.
 */

#ifndef SRC_PROTOCOL_LAYER_SHELL_LAYER_SURFACE_H_
#define SRC_PROTOCOL_LAYER_SHELL_LAYER_SURFACE_H_

#include "src/utils/signal_listener.h"
#include "src/wlroots.h"

namespace flakewm {
namespace core {

class CompositorPrivate;

class LayerSurface final {
 public:
  LayerSurface(CompositorPrivate* compositor,
               wlr_layer_surface_v1* layer_surface);

  LayerSurface(const LayerSurface&) = delete;
  LayerSurface& operator=(const LayerSurface&) = delete;

 private:
  friend class CompositorPrivate;

  static void OnMap(LayerSurface* layer_surface, void*);
  static void OnUnmap(LayerSurface* layer_surface, void*);
  static void OnCommit(LayerSurface* layer_surface, void*);
  static void OnNewPopup(LayerSurface* layer_surface, wlr_xdg_popup* popup);
  static void OnDestroy(LayerSurface* layer_surface, void*);
  static void NotifySurfaceScale(wlr_surface* surface, int, int, void* data);

  void NotifyOutputScale() const;
  void UnconstrainPopup(wlr_xdg_popup* popup) const;

  CompositorPrivate* compositor;
  wlr_layer_surface_v1* handle;
  wlr_scene_layer_surface_v1* scene_surface = nullptr;
  bool mapped = false;
  utils::SignalListener<LayerSurface, void> map{this, OnMap};
  utils::SignalListener<LayerSurface, void> unmap{this, OnUnmap};
  utils::SignalListener<LayerSurface, void> commit{this, OnCommit};
  utils::SignalListener<LayerSurface, wlr_xdg_popup> new_popup{this,
                                                               OnNewPopup};
  utils::SignalListener<LayerSurface, void> destroy{this, OnDestroy};
};

}  // namespace core
}  // namespace flakewm

#endif  // SRC_PROTOCOL_LAYER_SHELL_LAYER_SURFACE_H_
