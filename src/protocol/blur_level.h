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
 * gxde-wlcom's blur strength ladder, shared by the protocol families that use
 * it.
 */

#ifndef SRC_PROTOCOL_BLUR_LEVEL_H_
#define SRC_PROTOCOL_BLUR_LEVEL_H_

#include <algorithm>
#include <array>
#include <cstddef>

namespace flakewm {
namespace protocol {

// A rung of gxde-wlcom's ladder.  The pyramid depth is part of the setting, not
// a constant: its `calculate_blur_radius()` is `offset * 2^(iterations + 1)`,
// so the same offset is twice the radius at the next depth up.  A table that
// keeps only the offset column silently loses that, which is why this one is
// shared rather than copied per protocol -- it already cost the KDE path its
// whole iterations column once.
struct BlurLevel {
  int iterations;
  float offset;
};

// gxde-wlcom carries this table twice, as `blur_levels` in src/effect/blur.c
// (indexed by the `blur_strength` config option, default 4, range 1..15) and
// again in src/view/ukui_blur.c (indexed by the ukui-blur `set_level` request,
// same range).  The numbers are the same in both, so this is both of them.
inline constexpr std::array<BlurLevel, 15> kBlurLevels = {{
    {1, 1.5F},
    {1, 2.0F},
    {2, 2.5F},
    {2, 3.0F},
    {3, 2.6F},
    {3, 3.2F},
    {3, 3.8F},
    {3, 4.4F},
    {3, 5.0F},
    {4, 3.83333F},
    {4, 4.66667F},
    {4, 5.5F},
    {4, 6.33333F},
    {4, 7.16667F},
    {4, 8.0F},
}};

// Levels are numbered from one.  gxde-wlcom indexes the array with whatever the
// client or the config handed it; the clamp keeps a level that is out of range
// from reading past the table.
inline const BlurLevel& BlurLevelFor(int level) {
  return kBlurLevels[static_cast<size_t>(
      std::clamp(level, 1, static_cast<int>(kBlurLevels.size())) - 1)];
}

}  // namespace protocol
}  // namespace flakewm

#endif  // SRC_PROTOCOL_BLUR_LEVEL_H_
