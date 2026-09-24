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

#ifndef SRC_VIEW_SHAKE_CURSOR_SHAKE_CURSOR_H_
#define SRC_VIEW_SHAKE_CURSOR_SHAKE_CURSOR_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>

#include "src/wlr_wrapper/wlroots.h"

namespace flakewm {
namespace view {

class ShakeCursor final {
 public:
  using CursorManager = std::function<wlr_xcursor_manager*()>;
  using LockCursor = std::function<void(bool locked)>;

  ShakeCursor(wl_event_loop* loop, wlr_scene_tree* parent, wlr_cursor* cursor,
              CursorManager cursor_manager, LockCursor lock_cursor);
  ~ShakeCursor();

  ShakeCursor(const ShakeCursor&) = delete;
  ShakeCursor& operator=(const ShakeCursor&) = delete;

  void SetEnabled(bool enabled);
  bool IsEnabled() const { return enabled_; }

  // Call after the cursor moved
  void HandleMotion();
  // The xcursor theme or size changed
  void ReloadImage();

 private:
  enum class Stage : uint8_t { kHidden, kShowing, kShown, kHiding };

  struct Point {
    double x = 0;
    double y = 0;
    uint32_t time_msec = 0;
  };

  static constexpr size_t kPointCount = 64;

  static int OnTimer(void* data);
  bool DetectShake(double x, double y, uint32_t time_msec);
  void Update();
  bool EnsureImage();
  void DropImage();
  void Hide();

  wlr_scene_tree* tree_ = nullptr;
  wlr_scene_buffer* image_ = nullptr;
  wlr_cursor* cursor_ = nullptr;
  CursorManager cursor_manager_;
  LockCursor lock_cursor_;
  wl_event_source* timer_ = nullptr;
  bool enabled_ = false;

  // Pointer history used by the shake detector (a ring buffer).
  std::array<Point, kPointCount> points_ = {};
  size_t head_ = 0;
  size_t tail_ = 0;
  size_t count_ = 0;

  Stage stage_ = Stage::kHidden;
  bool cursor_locked_ = false;
  uint32_t last_shake_time_ = 0;
  uint32_t last_motion_time_ = 0;
  uint32_t animation_start_time_ = 0;

  // Enlarged image geometry in layout pixels.
  int image_width_ = 0;
  int image_height_ = 0;
  int image_hotspot_x_ = 0;
  int image_hotspot_y_ = 0;
  // Regular cursor size, which the animation grows from and shrinks to.
  int cursor_size_ = 0;
};

}  // namespace view
}  // namespace flakewm

#endif  // SRC_VIEW_SHAKE_CURSOR_SHAKE_CURSOR_H_
