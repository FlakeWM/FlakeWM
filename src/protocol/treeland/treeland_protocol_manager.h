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
 * Originally copyright by (C) 2024-2026 UnionTech Software Technology Co., Ltd.
 * Original license: Apache-2.0 OR LGPL-3.0-only OR GPL-2.0-only OR
 *                   GPL-3.0-only.
 * Redistributed with GPL-3.0-only.
 * Original code is modified to adapt C++ and Wlroots 0.20.2.
 */

#ifndef SRC_PROTOCOL_TREELAND_TREELAND_PROTOCOL_MANAGER_H_
#define SRC_PROTOCOL_TREELAND_TREELAND_PROTOCOL_MANAGER_H_

#include <memory>

#include "src/wlr_wrapper/wlroots.h"

namespace flakewm {
namespace core {
class CompositorPrivate;
}  // namespace core

namespace protocol {

class ProtocolManager;
class TreelandProtocolManagerImpl;

// Compatibility manager for the Treeland protocols consumed by GXDE/DTK.
class TreelandProtocolManager final {
 public:
  TreelandProtocolManager(core::CompositorPrivate* compositor,
                          ProtocolManager* protocol_manager);
  ~TreelandProtocolManager();

  TreelandProtocolManager(const TreelandProtocolManager&) = delete;
  TreelandProtocolManager& operator=(const TreelandProtocolManager&) = delete;

  bool Create(wl_display* display, wlr_seat* seat,
              wlr_output_layout* output_layout);

 private:
  std::unique_ptr<TreelandProtocolManagerImpl> impl_;
};

}  // namespace protocol
}  // namespace flakewm

#endif  // SRC_PROTOCOL_TREELAND_TREELAND_PROTOCOL_MANAGER_H_
