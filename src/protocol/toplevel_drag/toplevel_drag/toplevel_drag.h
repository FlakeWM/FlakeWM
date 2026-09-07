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
 * Originally copyright by (C) 2024 KylinSoft Co., Ltd.
 * Original license: GPL-1.0-or-later, see Open Kylin Wayland Compositor.
 * Redistributed with GPL-3.0-or-later.
 * Original code is modified to adapt C++ and Wlroots 0.20.2.
 */

#ifndef SRC_PROTOCOL_TOPLEVEL_DRAG_TOPLEVEL_DRAG_TOPLEVEL_DRAG_H_
#define SRC_PROTOCOL_TOPLEVEL_DRAG_TOPLEVEL_DRAG_TOPLEVEL_DRAG_H_

#include "protocol/xdg-toplevel-drag-v1-protocol.h"
#include "src/utils/signal_listener.h"
#include "src/wlr_wrapper/wlr_layer_shell.h"
#include "src/wlr_wrapper/wlroots.h"

namespace flakewm {
namespace protocol {

class ToplevelDragManager;

class ToplevelDrag final {
 public:
  ToplevelDrag(ToplevelDragManager* manager, wl_resource* resource,
               wlr_data_source* source);
  ~ToplevelDrag();

  ToplevelDrag(const ToplevelDrag&) = delete;
  ToplevelDrag& operator=(const ToplevelDrag&) = delete;

  wlr_data_source* Source() const;
  wlr_xdg_toplevel* Toplevel() const;
  int OffsetX() const;
  int OffsetY() const;
  void MakeInert();

 private:
  static void Destroy(wl_client* client, wl_resource* resource);
  static void Attach(wl_client* client, wl_resource* resource,
                     wl_resource* toplevel, int32_t x_offset, int32_t y_offset);
  static void OnResourceDestroyed(wl_resource* resource);
  static void OnSourceDestroyed(ToplevelDrag* drag, void*);
  static void OnToplevelGone(ToplevelDrag* drag, void*);

  static const struct xdg_toplevel_drag_v1_interface kImplementation;

  ToplevelDragManager* manager_;
  wl_resource* resource_;
  wlr_data_source* source_;
  wlr_xdg_toplevel* toplevel_ = nullptr;
  int32_t offset_x_ = 0;
  int32_t offset_y_ = 0;
  utils::SignalListener<ToplevelDrag, void> source_destroy_{this,
                                                            OnSourceDestroyed};
  utils::SignalListener<ToplevelDrag, void> toplevel_unmap_{this,
                                                            OnToplevelGone};
  utils::SignalListener<ToplevelDrag, void> toplevel_destroy_{this,
                                                              OnToplevelGone};
};

}  // namespace protocol
}  // namespace flakewm

#endif  // SRC_PROTOCOL_TOPLEVEL_DRAG_TOPLEVEL_DRAG_TOPLEVEL_DRAG_H_
