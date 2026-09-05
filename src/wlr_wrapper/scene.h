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
 * Wlroots scene wrapper.
 */

#ifndef SRC_WLR_WRAPPER_SCENE_H_
#define SRC_WLR_WRAPPER_SCENE_H_

// color.h must be parsed before wlr_renderer.h includes it.
// NOLINTBEGIN(build/include_order)
#include "src/wlr_wrapper/color.h"

extern "C" {
#include <pixman.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_damage_ring.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/util/addon.h>
#include <wlr/util/box.h>

// C permits static array bounds in parameters; C++ does not.
#define static
#include <wlr/types/wlr_scene.h>
#undef static
}
// NOLINTEND

#endif  // SRC_WLR_WRAPPER_SCENE_H_
