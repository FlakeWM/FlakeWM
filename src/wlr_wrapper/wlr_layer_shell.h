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
 * C++ compatibility for the layer shell protocol header.
 */

#ifndef SRC_WLR_WRAPPER_WLR_LAYER_SHELL_H_
#define SRC_WLR_WRAPPER_WLR_LAYER_SHELL_H_

// The protocol calls this field "namespace", which is reserved by C++.
#define namespace namespace_
#include <wlr/types/wlr_layer_shell_v1.h>
#undef namespace

#endif  // SRC_WLR_WRAPPER_WLR_LAYER_SHELL_H_
