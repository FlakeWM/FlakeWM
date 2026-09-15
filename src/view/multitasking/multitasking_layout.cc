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

#include <algorithm>
#include <cmath>
#include <vector>

#include "src/view/multitasking/multitasking.h"

namespace flakewm {
namespace view {
namespace {

constexpr double kFirstWindowScale = 720.0 / 1080.0;
constexpr double kSpacingHorizontalScale = 20.0 / 1920.0;
constexpr double kSpacingVerticalScale = 20.0 / 1080.0;

}  // namespace

void Multitasking::CalculateWindowLayout() {
  window_placements_.clear();
  if (visible_entries_.empty()) return;

  const int count = static_cast<int>(visible_entries_.size());
  const int spacing_x = std::max(
      1,
      static_cast<int>(std::lround(screen_.width * kSpacingHorizontalScale)));
  const int spacing_y = std::max(
      1, static_cast<int>(std::lround(screen_.height * kSpacingVerticalScale)));
  const int content_y = layout_.window_area_y;
  const int content_height = screen_.height - content_y;
  double scale_height = screen_.height * kFirstWindowScale;
  int rows = 1;
  int used_rows = 1;
  bool overlap = false;
  do {
    overlap = false;
    double row_width = spacing_x;
    used_rows = 1;
    for (const Entry& entry : visible_entries_) {
      double width = std::max(1, entry.geometry.width);
      const double height = std::max(1, entry.geometry.height);
      if (height > scale_height) width *= scale_height / height;
      row_width += width + spacing_x;
      if (row_width > screen_.width) {
        ++used_rows;
        if (used_rows > rows) {
          overlap = true;
          break;
        }
        row_width = spacing_x + width + spacing_x;
      }
    }
    if (overlap) {
      scale_height -= 15.0;
      const double critical =
          static_cast<double>(content_height - (rows + 2) * spacing_y) /
          (rows + 1);
      if (scale_height <= critical) ++rows;
    }
  } while (overlap && scale_height > 32.0);

  std::vector<double> row_widths(static_cast<std::size_t>(used_rows),
                                 spacing_x);
  int row = 0;
  for (const Entry& entry : visible_entries_) {
    double width = std::max(1, entry.geometry.width);
    const double height = std::max(1, entry.geometry.height);
    if (height > scale_height) width *= scale_height / height;
    if (row_widths[row] + width + spacing_x > screen_.width &&
        row + 1 < used_rows) {
      ++row;
    }
    row_widths[row] += width + spacing_x;
  }

  double y = content_y + (content_height - (used_rows - 1) * spacing_y -
                          used_rows * scale_height) /
                             2.0;
  row = 0;
  double x = (screen_.width - row_widths[0]) / 2.0 + spacing_x;
  double used_width = spacing_x;
  window_placements_.reserve(visible_entries_.size());
  for (const Entry& entry : visible_entries_) {
    double width = std::max(1, entry.geometry.width);
    double height = std::max(1, entry.geometry.height);
    if (height > scale_height) {
      const double scale = scale_height / height;
      width *= scale;
      height = scale_height;
    }
    if (used_width + width + spacing_x > screen_.width && row + 1 < used_rows) {
      ++row;
      used_width = spacing_x;
      x = (screen_.width - row_widths[row]) / 2.0 + spacing_x;
      y += scale_height + spacing_y;
    }
    window_placements_.push_back({
        .x = static_cast<int>(std::lround(x)),
        .y = static_cast<int>(std::lround(y + (scale_height - height) / 2.0)),
        .width = static_cast<int>(std::lround(width)),
        .height = static_cast<int>(std::lround(height)),
    });
    x += width + spacing_x;
    used_width += width + spacing_x;
  }
}

}  // namespace view
}  // namespace flakewm
