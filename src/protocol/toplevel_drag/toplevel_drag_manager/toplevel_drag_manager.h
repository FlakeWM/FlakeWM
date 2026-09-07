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

#ifndef SRC_PROTOCOL_TOPLEVEL_DRAG_TOPLEVEL_DRAG_MANAGER_TOPLEVEL_DRAG_MANAGER_H_
#define SRC_PROTOCOL_TOPLEVEL_DRAG_TOPLEVEL_DRAG_MANAGER_TOPLEVEL_DRAG_MANAGER_H_

#include <list>
#include <memory>

#include "protocol/xdg-toplevel-drag-v1-protocol.h"
#include "src/protocol/toplevel_drag/toplevel_drag/toplevel_drag.h"

namespace flakewm {
namespace protocol {

class ToplevelDragManager final {
 public:
  explicit ToplevelDragManager(wl_display* display);
  ~ToplevelDragManager();

  ToplevelDragManager(const ToplevelDragManager&) = delete;
  ToplevelDragManager& operator=(const ToplevelDragManager&) = delete;

  bool IsValid() const;
  ToplevelDrag* Find(wlr_data_source* source) const;
  void Remove(ToplevelDrag* drag);

 private:
  static void Bind(wl_client* client, void* data, uint32_t version,
                   uint32_t id);
  static void Destroy(wl_client* client, wl_resource* resource);
  static void GetToplevelDrag(wl_client* client, wl_resource* resource,
                              uint32_t id, wl_resource* data_source);

  static const struct xdg_toplevel_drag_manager_v1_interface kImplementation;

  wl_global* global_ = nullptr;
  std::list<std::unique_ptr<ToplevelDrag>> drags_;
};

}  // namespace protocol
}  // namespace flakewm

#endif  // SRC_PROTOCOL_TOPLEVEL_DRAG_TOPLEVEL_DRAG_MANAGER_TOPLEVEL_DRAG_MANAGER_H_
