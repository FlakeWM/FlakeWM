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

#include "src/view/shake_cursor/shake_cursor.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <utility>

#include "src/view/ssd/ssd_buffer/ssd_buffer.h"

namespace flakewm {
namespace view {
namespace {

// Timings and thresholds from GXWM's shake_cursor.c.
constexpr uint32_t kInterval = 500;  // ms of how long shaked
constexpr uint32_t kDuration = 150;  // grow/shrink animation, ms
constexpr double kMinDiagonal = 100;
constexpr double kShakeFactor = 4;
constexpr float kImageScale = 4.0F;
constexpr int kFrameMsec = 16;

uint32_t NowMsec() {
  return static_cast<uint32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

// Movements within the tolerance count as movement in any direction.
bool InSameSign(double a, double b) {
  constexpr double kTolerance = 1;
  return (a >= -kTolerance && b >= -kTolerance) ||
         (a <= kTolerance && b <= kTolerance);
}

// CSS ease-in-out, cubic-bezier(0.42, 0, 0.58, 1), as in GXWM's animator.
double EaseInOut(double progress) {
  progress = std::clamp(progress, 0.0, 1.0);
  const auto bezier = [](double p1, double p2, double t) {
    const double u = 1 - t;
    return 3 * u * u * t * p1 + 3 * u * t * t * p2 + t * t * t;
  };
  double low = 0;
  double high = 1;
  for (int i = 0; i < 24; ++i) {
    const double mid = (low + high) / 2;
    if (bezier(0.42, 0.58, mid) < progress) {
      low = mid;
    } else {
      high = mid;
    }
  }
  return bezier(0, 1, (low + high) / 2);
}

}  // namespace

ShakeCursor::ShakeCursor(wl_event_loop* loop, wlr_scene_tree* parent,
                         wlr_cursor* cursor, CursorManager cursor_manager,
                         LockCursor lock_cursor)
    : cursor_(cursor),
      cursor_manager_(std::move(cursor_manager)),
      lock_cursor_(std::move(lock_cursor)) {
  if (parent != nullptr) tree_ = wlr_scene_tree_create(parent);
  if (loop != nullptr) timer_ = wl_event_loop_add_timer(loop, OnTimer, this);
}

ShakeCursor::~ShakeCursor() {
  Hide();
  if (timer_ != nullptr) wl_event_source_remove(timer_);
  DropImage();
  if (tree_ != nullptr) wlr_scene_node_destroy(&tree_->node);
}

void ShakeCursor::SetEnabled(bool enabled) {
  if (enabled_ == enabled) return;
  enabled_ = enabled;
  count_ = 0;
  head_ = tail_ = 0;
  if (!enabled_) Hide();
}

void ShakeCursor::HandleMotion() {
  if (!enabled_ || cursor_ == nullptr) return;
  const uint32_t now = NowMsec();
  last_motion_time_ = now;
  if (DetectShake(cursor_->x, cursor_->y, now)) last_shake_time_ = now;
  Update();
}

void ShakeCursor::ReloadImage() {
  DropImage();
  if (stage_ != Stage::kHidden) Update();
}

// KWin's detector, via GXWM: the pointer was shaken when its trail over the
// last kInterval ms is much longer than the diagonal of the trail's bounds.
bool ShakeCursor::DetectShake(double x, double y, uint32_t time_msec) {
  // Drop history older than the interval.
  while (count_ > 0 && time_msec - points_[head_].time_msec >= kInterval) {
    head_ = (head_ + 1) % kPointCount;
    --count_;
  }

  // Merge a sample that continues the previous direction.
  if (count_ >= 2) {
    Point& last = points_[(tail_ + kPointCount - 1) % kPointCount];
    const Point& prev = points_[(tail_ + kPointCount - 2) % kPointCount];
    if (InSameSign(last.x - prev.x, x - last.x) &&
        InSameSign(last.y - prev.y, y - last.y)) {
      last = {x, y, time_msec};
      return false;
    }
  }

  if (count_ == kPointCount) {
    head_ = (head_ + 1) % kPointCount;
    --count_;
  }
  points_[tail_] = {x, y, time_msec};
  tail_ = (tail_ + 1) % kPointCount;
  ++count_;
  if (count_ < 2) return false;

  double left = points_[head_].x;
  double right = left;
  double top = points_[head_].y;
  double bottom = top;
  double distance = 0;
  size_t index = head_;
  for (size_t i = 1; i < count_; ++i) {
    const Point& current = points_[index];
    index = (index + 1) % kPointCount;
    const Point& next = points_[index];
    distance += std::hypot(next.x - current.x, next.y - current.y);
    left = std::min(left, next.x);
    right = std::max(right, next.x);
    top = std::min(top, next.y);
    bottom = std::max(bottom, next.y);
  }

  const double diagonal = std::hypot(right - left, bottom - top);
  if (diagonal < kMinDiagonal || distance / diagonal <= kShakeFactor) {
    return false;
  }
  head_ = tail_;
  count_ = 0;
  return true;
}

int ShakeCursor::OnTimer(void* data) {
  static_cast<ShakeCursor*>(data)->Update();
  return 0;
}

void ShakeCursor::Update() {
  const uint32_t now = NowMsec();
  const Stage old_stage = stage_;
  if (now - last_shake_time_ < kInterval) {
    if (stage_ == Stage::kHidden || stage_ == Stage::kHiding) {
      stage_ = Stage::kShowing;
      animation_start_time_ = now;
    } else if (stage_ == Stage::kShowing &&
               now - animation_start_time_ > kDuration) {
      stage_ = Stage::kShown;
    }
  } else if (now - last_motion_time_ > kInterval / 2) {
    if (stage_ == Stage::kShowing || stage_ == Stage::kShown) {
      stage_ = Stage::kHiding;
      animation_start_time_ = now;
    } else if (stage_ == Stage::kHiding &&
               now - animation_start_time_ > kDuration) {
      stage_ = Stage::kHidden;
    }
  }

  if (stage_ != Stage::kHidden && !EnsureImage()) stage_ = Stage::kHidden;

  if (stage_ == Stage::kHidden) {
    if (old_stage != Stage::kHidden) Hide();
    return;
  }
  if (!cursor_locked_) {
    cursor_locked_ = true;
    if (lock_cursor_) lock_cursor_(true);
    if (tree_ != nullptr) wlr_scene_node_raise_to_top(&tree_->node);
  }

  // Grow from, or shrink back to, the regular cursor size around the
  // hotspot so the arrow tip stays under the pointer.
  const double min_scale =
      std::min(1.0, static_cast<double>(cursor_size_) / image_height_);
  double scale = 1;
  const double progress =
      static_cast<double>(now - animation_start_time_) / kDuration;
  if (stage_ == Stage::kShowing) {
    scale = min_scale + (1 - min_scale) * EaseInOut(progress);
  } else if (stage_ == Stage::kHiding) {
    scale = 1 - (1 - min_scale) * EaseInOut(progress);
  }
  const int width = std::max(1, static_cast<int>(image_width_ * scale));
  const int height = std::max(1, static_cast<int>(image_height_ * scale));
  wlr_scene_buffer_set_dest_size(image_, width, height);
  wlr_scene_node_set_position(
      &image_->node,
      static_cast<int>(std::lround(cursor_->x - image_hotspot_x_ * scale)),
      static_cast<int>(std::lround(cursor_->y - image_hotspot_y_ * scale)));
  wlr_scene_node_set_enabled(&image_->node, true);
  if (timer_ != nullptr) wl_event_source_timer_update(timer_, kFrameMsec);
}

bool ShakeCursor::EnsureImage() {
  if (image_ != nullptr) return true;
  wlr_xcursor_manager* manager = cursor_manager_ ? cursor_manager_() : nullptr;
  if (tree_ == nullptr || manager == nullptr ||
      !wlr_xcursor_manager_load(manager, kImageScale)) {
    return false;
  }
  // Some themes (e.g. DMZ) only ship the legacy left_ptr name.
  const char* name = "default";
  wlr_xcursor* xcursor =
      wlr_xcursor_manager_get_xcursor(manager, name, kImageScale);
  if (xcursor == nullptr) {
    name = "left_ptr";
    xcursor = wlr_xcursor_manager_get_xcursor(manager, name, kImageScale);
  }
  if (xcursor == nullptr || xcursor->image_count == 0) return false;
  const wlr_xcursor_image* image = xcursor->images[0];

  SsdBuffer* buffer = SsdBuffer::Create(static_cast<int>(image->width),
                                        static_cast<int>(image->height));
  if (buffer == nullptr) return false;
  // Xcursor pixels are premultiplied ARGB32, the same layout as the buffer.
  QImage& pixels = buffer->Image();
  for (uint32_t row = 0; row < image->height; ++row) {
    std::memcpy(pixels.scanLine(static_cast<int>(row)),
                image->buffer + static_cast<size_t>(row) * image->width * 4,
                static_cast<size_t>(image->width) * 4);
  }
  image_ = wlr_scene_buffer_create(tree_, buffer->Handle());
  wlr_buffer_drop(buffer->Handle());
  if (image_ == nullptr) return false;
  wlr_scene_node_set_enabled(&image_->node, false);

  image_width_ = static_cast<int>(image->width);
  image_height_ = static_cast<int>(image->height);
  image_hotspot_x_ = static_cast<int>(image->hotspot_x);
  image_hotspot_y_ = static_cast<int>(image->hotspot_y);
  cursor_size_ = static_cast<int>(manager->size);
  return true;
}

void ShakeCursor::DropImage() {
  if (image_ == nullptr) return;
  wlr_scene_node_destroy(&image_->node);
  image_ = nullptr;
}

void ShakeCursor::Hide() {
  if (timer_ != nullptr) wl_event_source_timer_update(timer_, 0);
  if (image_ != nullptr) wlr_scene_node_set_enabled(&image_->node, false);
  stage_ = Stage::kHidden;
  if (cursor_locked_) {
    cursor_locked_ = false;
    if (lock_cursor_) lock_cursor_(false);
  }
}

}  // namespace view
}  // namespace flakewm
