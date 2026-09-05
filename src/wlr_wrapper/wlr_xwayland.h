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
 * C++ XWayland include header.
 */

#ifndef SRC_WLR_WRAPPER_WLR_XWAYLAND_H_
#define SRC_WLR_WRAPPER_WLR_XWAYLAND_H_

#include <wayland-server-core.h>
#include <wlr/util/addon.h>
#include <wlr/xwayland/server.h>
#include <xcb/xcb.h>
#include <xcb/xcb_ewmh.h>
#include <xcb/xcb_icccm.h>

#define class class_
#include <wlr/xwayland/xwayland.h>
#undef class

#endif  // SRC_WLR_WRAPPER_WLR_XWAYLAND_H_
