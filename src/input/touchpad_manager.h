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

#ifndef SRC_INPUT_TOUCHPAD_MANAGER_H_
#define SRC_INPUT_TOUCHPAD_MANAGER_H_

#include <QDBusConnection>
#include <QTimer>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "src/wlr_wrapper/wlroots.h"

#ifdef FLAKEWM_HAS_QGSETTINGS
class QGSettings;
#endif

namespace flakewm {
namespace input {

class TouchpadManager final {
 public:
  using GestureHandler = std::function<bool(
      const char* type, const char* device, const char* direction,
      std::uint32_t fingers, const char* edge, const char* stage,
      const char* follow_direction, double dx, double dy)>;

  TouchpadManager();
  ~TouchpadManager();

  TouchpadManager(const TouchpadManager&) = delete;
  TouchpadManager& operator=(const TouchpadManager&) = delete;

  void AddDevice(wlr_input_device* device);
  bool ShouldForwardAxis(const wlr_pointer_axis_event& event) const;

  void BeginSwipe(std::uint32_t fingers);
  void UpdateSwipe(double delta_x, double delta_y);
  bool EndSwipe(bool cancelled);
  void BeginPinch(std::uint32_t fingers);
  void UpdatePinch(const wlr_pointer_pinch_update_event& event);
  bool EndPinch(bool cancelled);
  void BeginHold(std::uint32_t fingers);
  bool EndHold(bool cancelled);
  void TouchDown(wlr_touch* touch, std::int32_t id, double x, double y);
  void TouchMotion(wlr_touch* touch, std::int32_t id, double x, double y);
  void TouchUp(wlr_touch* touch, std::int32_t id, bool cancelled);
  void SetGestureHandler(GestureHandler handler);

 private:
  struct Device;

  enum class GestureType : std::uint8_t { kNone, kSwipe, kPinch, kHold };

  struct Settings {
    bool enabled = true;
    bool left_handed = false;
    bool disable_while_typing = true;
    bool natural_scroll = false;
    bool edge_scroll = true;
    bool horizontal_scroll = false;
    bool vertical_scroll = true;
    bool tap_to_click = true;
    double acceleration_factor = 1.0;
  };

  struct GestureState {
    GestureType type = GestureType::kNone;
    std::uint32_t fingers = 0;
    double delta_x = 0;
    double delta_y = 0;
    double scale = 1;
    double rotation = 0;
    double follow_dx = 0;
    double follow_dy = 0;
    std::string direction = "none";
    bool triggered = false;
    bool handled = false;
  };

  struct TouchPoint {
    wlr_touch* touch = nullptr;
    std::int32_t id = 0;
    double initial_x = 0;
    double initial_y = 0;
    double x = 0;
    double y = 0;
    double last_x = 0;
    double last_y = 0;
    bool moved = false;
  };

  void LoadSettings();
  void ApplySettings();
  void ApplySettings(Device* device);
  const Device* FindDevice(const wlr_pointer* pointer) const;
  bool ExecuteGesture(const char* name, const char* direction,
                      std::uint32_t fingers, const char* stage = "trigger",
                      const char* follow_direction = "none", double dx = 0,
                      double dy = 0, const char* device = "touchpad",
                      const char* edge = "none") const;
  const char* TouchEdge() const;
  void ResetTouchGesture();
  void ResetGesture();

  QDBusConnection gesture_connection_;
#ifdef FLAKEWM_HAS_QGSETTINGS
  std::unique_ptr<QGSettings> settings_source_;
  std::unique_ptr<QGSettings> gesture_settings_source_;
#endif
  std::vector<std::unique_ptr<Device>> devices_;
  Settings settings_;
  GestureState gesture_;
  GestureState touch_gesture_;
  std::vector<TouchPoint> touch_points_;
  GestureHandler gesture_handler_;
  QTimer touch_hold_timer_;
  bool gestures_enabled_ = true;
};

}  // namespace input
}  // namespace flakewm

#endif  // SRC_INPUT_TOUCHPAD_MANAGER_H_
