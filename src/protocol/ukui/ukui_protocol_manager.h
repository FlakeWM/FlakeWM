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

#ifndef SRC_PROTOCOL_UKUI_UKUI_PROTOCOL_MANAGER_H_
#define SRC_PROTOCOL_UKUI_UKUI_PROTOCOL_MANAGER_H_

#include <memory>

#include "src/wlr_wrapper/wlroots.h"

namespace flakewm {
namespace core {
class CompositorPrivate;
}  // namespace core

namespace protocol {

class UkuiProtocolManager final {
 public:
  explicit UkuiProtocolManager(core::CompositorPrivate* compositor);
  ~UkuiProtocolManager();

  UkuiProtocolManager(const UkuiProtocolManager&) = delete;
  UkuiProtocolManager& operator=(const UkuiProtocolManager&) = delete;

  bool Create(wl_display* display, wlr_seat* seat,
              wlr_output_layout* output_layout);
  void AddOutput(wlr_output* output);
  void UpdateOutputs();
  void MapToplevel(wlr_surface* surface);
  void UnmapToplevel(wlr_surface* surface);
  void UpdateToplevel(wlr_surface* surface);
  void UpdateToplevelParent(wlr_surface* surface, wlr_surface* parent);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace protocol
}  // namespace flakewm

#endif  // SRC_PROTOCOL_UKUI_UKUI_PROTOCOL_MANAGER_H_
