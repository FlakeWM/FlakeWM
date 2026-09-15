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
 * The file is adapted from GXDE KWin's Window Preview screen.
 */

#include <algorithm>
#include <cmath>

#include "src/view/window_previews/window_previews.h"

namespace flakewm {
namespace view {
namespace {

constexpr double kMargin = 20.0;
constexpr double kAccuracy = 20.0;
constexpr double kOverlapPad = 5.0;
constexpr double kMaxScale = 2.0;

bool Overlaps(const WindowPreviewBox& a, const WindowPreviewBox& b,
              double pad) {
  return a.x < b.x + b.width + pad && a.x + a.width + pad > b.x &&
         a.y < b.y + b.height + pad && a.y + a.height + pad > b.y;
}

WindowPreviewBox Unite(const WindowPreviewBox& a, const WindowPreviewBox& b) {
  const double left = std::min(a.x, b.x);
  const double top = std::min(a.y, b.y);
  const double right = std::max(a.x + a.width, b.x + b.width);
  const double bottom = std::max(a.y + a.height, b.y + b.height);
  return {left, top, right - left, bottom - top};
}

double HeightForWidth(const WindowPreviewBox& original, double width) {
  return original.width <= 0.0 ? 0.0 : width * original.height / original.width;
}

bool Blocked(const WindowPreviewBox& candidate, std::size_t self,
             const std::vector<WindowPreviewBox>& targets,
             const WindowPreviewBox& inner) {
  if (candidate.x < inner.x || candidate.y < inner.y ||
      candidate.x + candidate.width > inner.x + inner.width ||
      candidate.y + candidate.height > inner.y + inner.height) {
    return true;
  }
  for (std::size_t index = 0; index < targets.size(); ++index) {
    if (index != self && Overlaps(candidate, targets[index], kOverlapPad)) {
      return true;
    }
  }
  return false;
}

}  // namespace

void WindowPreviews::CalculateNaturalLayout(
    std::vector<WindowPreviewBox>* boxes) const {
  if (boxes == nullptr || boxes->empty()) return;
  std::vector<WindowPreviewBox>& targets = *boxes;
  const std::vector<WindowPreviewBox> original = targets;
  if (targets.size() == 1) {
    const WindowPreviewBox& only = targets.front();
    if (only.x >= 0 && only.y >= 0 && only.x + only.width <= screen_.width &&
        only.y + only.height <= screen_.height) {
      return;
    }
  }

  WindowPreviewBox bounds = {static_cast<double>(usable_.x - screen_.x),
                             static_cast<double>(usable_.y - screen_.y),
                             static_cast<double>(usable_.width),
                             static_cast<double>(usable_.height)};
  std::vector<int> direction(targets.size());
  for (std::size_t index = 0; index < targets.size(); ++index) {
    bounds = Unite(bounds, targets[index]);
    direction[index] = static_cast<int>(index % 4);
  }

  bool overlap = false;
  int iterations = 0;
  do {
    overlap = false;
    for (std::size_t w = 0; w < targets.size(); ++w) {
      for (std::size_t e = 0; e < targets.size(); ++e) {
        if (w == e || !Overlaps(targets[w], targets[e], kOverlapPad)) continue;
        overlap = true;
        double dx = targets[e].x + targets[e].width / 2.0 -
                    (targets[w].x + targets[w].width / 2.0);
        double dy = targets[e].y + targets[e].height / 2.0 -
                    (targets[w].y + targets[w].height / 2.0);
        if (dx == 0.0 && dy == 0.0) dx = 1.0;
        double distance = std::abs(dx) + std::abs(dy);
        dx *= kAccuracy / distance;
        dy *= kAccuracy / distance;
        targets[w].x -= dx;
        targets[w].y -= dy;
        targets[e].x += dx;
        targets[e].y += dy;

        int x_section =
            static_cast<int>((targets[w].x - bounds.x) / (bounds.width / 3.0));
        int y_section =
            static_cast<int>((targets[w].y - bounds.y) / (bounds.height / 3.0));
        if (x_section != 1 || y_section != 1) {
          if (x_section == 1) x_section = direction[w] / 2 ? 2 : 0;
          if (y_section == 1) y_section = direction[w] % 2 ? 2 : 0;
        }
        double px = 0.0;
        double py = 0.0;
        const double cx = targets[w].x + targets[w].width / 2.0;
        const double cy = targets[w].y + targets[w].height / 2.0;
        if (x_section == 0 && y_section == 0) {
          px = bounds.x - cx;
          py = bounds.y - cy;
        } else if (x_section == 2 && y_section == 0) {
          px = bounds.x + bounds.width - cx;
          py = bounds.y - cy;
        } else if (x_section == 2 && y_section == 2) {
          px = bounds.x + bounds.width - cx;
          py = bounds.y + bounds.height - cy;
        } else if (x_section == 0 && y_section == 2) {
          px = bounds.x - cx;
          py = bounds.y + bounds.height - cy;
        }
        if (px != 0.0 || py != 0.0) {
          distance = std::abs(px) + std::abs(py);
          targets[w].x += px * kAccuracy / distance;
          targets[w].y += py * kAccuracy / distance;
        }
        bounds = Unite(bounds, targets[w]);
        bounds = Unite(bounds, targets[e]);
      }
    }
  } while (overlap && ++iterations < 10000);

  const double area_x = usable_.x - screen_.x;
  const double area_y = usable_.y - screen_.y;
  double scale = 1.0;
  if (!(bounds.x == area_x && bounds.y == area_y &&
        bounds.width == usable_.width && bounds.height == usable_.height)) {
    scale = usable_.width / bounds.width < usable_.height / bounds.height
                ? (usable_.width - kMargin) / bounds.width
                : (usable_.height - kMargin) / bounds.height;
  }
  bounds = {(bounds.x * scale -
             (usable_.width - kMargin - bounds.width * scale) / 2.0 - 10.0) /
                scale,
            (bounds.y * scale -
             (usable_.height - kMargin - bounds.height * scale) / 2.0 - 10.0) /
                scale,
            usable_.width / scale, usable_.height / scale};
  for (WindowPreviewBox& target : targets) {
    target = {(target.x - bounds.x) * scale + area_x,
              (target.y - bounds.y) * scale + area_y, target.width * scale,
              target.height * scale};
  }

  const double inset = 10.0 / scale;
  const WindowPreviewBox inner = {area_x + inset, area_y + inset,
                                  usable_.width - 2.0 * inset,
                                  usable_.height - 2.0 * inset};
  bool moved = false;
  iterations = 0;
  do {
    moved = false;
    for (std::size_t w = 0; w < targets.size(); ++w) {
      const double width_diff = kAccuracy;
      double height_diff =
          HeightForWidth(original[w], targets[w].width + width_diff) -
          targets[w].height;
      const double x_diff = width_diff / 2.0;
      double y_diff = height_diff / 2.0;
      for (int attempt = 0; attempt < 4; ++attempt) {
        WindowPreviewBox candidate = targets[w];
        if (attempt == 0) {
          candidate.x += x_diff;
          candidate.y -= y_diff + height_diff;
        } else if (attempt == 1) {
          candidate.x += x_diff;
          candidate.y += y_diff;
        } else if (attempt == 2) {
          candidate.x -= x_diff + width_diff;
          candidate.y += y_diff;
        } else {
          candidate.x -= x_diff + width_diff;
          candidate.y -= y_diff + height_diff;
        }
        candidate.width += width_diff;
        candidate.height += height_diff;
        if (Blocked(candidate, w, targets, inner)) continue;
        targets[w] = candidate;
        moved = true;
        height_diff =
            HeightForWidth(original[w], targets[w].width + width_diff) -
            targets[w].height;
        y_diff = height_diff / 2.0;
      }
    }
  } while (moved && ++iterations < 10000);

  for (std::size_t index = 0; index < targets.size(); ++index) {
    if (original[index].width <= 0.0) continue;
    double item_scale = targets[index].width / original[index].width;
    if (item_scale > kMaxScale ||
        (item_scale > 1.0 &&
         (original[index].width > 300.0 || original[index].height > 300.0))) {
      item_scale =
          original[index].width > 300.0 || original[index].height > 300.0
              ? 1.0
              : kMaxScale;
      const double cx = targets[index].x + targets[index].width / 2.0;
      const double cy = targets[index].y + targets[index].height / 2.0;
      targets[index].width = original[index].width * item_scale;
      targets[index].height = original[index].height * item_scale;
      targets[index].x = cx - targets[index].width / 2.0;
      targets[index].y = cy - targets[index].height / 2.0;
    }
  }
}

}  // namespace view
}  // namespace flakewm
