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
 * Originally copyright by (C) 2024 KylinSoft Co., Ltd.
 * Originally copyright by (C) 2026 GXDE Team.
 * Original license: GPL-3.0-or-later, see GXDE Wayland Compositor.
 * Redistributed with GPL-3.0-or-later.
 * Original code is modified to adapt C++ and Wlroots 0.20.2.
 */

#ifndef SRC_VIEW_TOUCH_FEEDBACK_H_
#define SRC_VIEW_TOUCH_FEEDBACK_H_

#include <cstdint>
#include <memory>
#include <vector>

#include "src/wlr_wrapper/wlroots.h"

namespace flakewm {
namespace view {

class TouchFeedback final {
 public:
  struct Position {
    double x;
    double y;
  };

  static std::unique_ptr<TouchFeedback> Create(wlr_scene_tree* parent);
  ~TouchFeedback();

  TouchFeedback(const TouchFeedback&) = delete;
  TouchFeedback& operator=(const TouchFeedback&) = delete;

  void Down(wlr_touch* touch, int32_t touch_id, Position position);
  void Motion(wlr_touch* touch, int32_t touch_id, Position position);
  void Up(wlr_touch* touch, int32_t touch_id);
  void Cancel(wlr_touch* touch, int32_t touch_id);
  void CancelDevice(wlr_touch* touch);
  void Render();

 private:
  struct Point;

  explicit TouchFeedback(wlr_scene_tree* parent);
  bool IsValid() const;
  Point* FindPoint(wlr_touch* touch, int32_t touch_id) const;
  void RemovePoint(wlr_touch* touch, int32_t touch_id);

  wlr_scene_tree* tree_ = nullptr;
  std::vector<std::unique_ptr<Point>> points_;
};

}  // namespace view
}  // namespace flakewm

#endif  // SRC_VIEW_TOUCH_FEEDBACK_H_
