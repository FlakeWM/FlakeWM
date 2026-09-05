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
 * Wlroots color wrapper.
 */

#ifndef SRC_WLR_WRAPPER_COLOR_H_
#define SRC_WLR_WRAPPER_COLOR_H_

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

// C permits static array bounds in parameters; C++ does not.
extern "C" {
#define static
#include <wlr/render/color.h>
#undef static
}

#endif  // SRC_WLR_WRAPPER_COLOR_H_
