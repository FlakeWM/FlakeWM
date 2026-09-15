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
 * The layout is adapted from GXDE KWin's Multitasking screen.
 */

#ifndef SRC_VIEW_MULTITASKING_MULTITASKING_WALLPAPER_H_
#define SRC_VIEW_MULTITASKING_MULTITASKING_WALLPAPER_H_

namespace flakewm {
namespace view {

class SsdBuffer;

// Reproduces gxde-wlcom's wallpaper lookup and BRIGHTNESS=0.4 backdrop.
SsdBuffer* CreateMultitaskingWallpaper(int width, int height);
SsdBuffer* CreateMultitaskingWorkspaceWallpaper(SsdBuffer* wallpaper, int width,
                                                int height);

}  // namespace view
}  // namespace flakewm

#endif  // SRC_VIEW_MULTITASKING_MULTITASKING_WALLPAPER_H_
