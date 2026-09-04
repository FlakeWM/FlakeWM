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
 * This file provides some misc utilities.
 */

#ifndef SRC_UTILS_MISC_MISC_H_
#define SRC_UTILS_MISC_MISC_H_

#include <wayland-server.h>

#include <optional>
#include <string>

namespace flakewm {
namespace utils {

class Misc {
 public:
  static bool DropProcessPrivilegePermissions();
  static std::optional<std::string> SelectWaylandSocket(wl_display* display);
  static void HandleSignal(int signal);
};

}  // namespace utils
}  // namespace flakewm

#endif  // SRC_UTILS_MISC_MISC_H_
