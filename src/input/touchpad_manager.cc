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

#include <absl/log/absl_log.h>
#include <libinput.h>

#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QObject>
#include <QString>
#include <algorithm>
#include <cmath>
#include <memory>

#ifdef FLAKEWM_HAS_QGSETTINGS
#include <QGSettings/QGSettings>
#endif

#include "src/utils/signal_listener.h"
#include "src/input/touchpad_manager.h"

namespace flakewm {
namespace input {
namespace {

constexpr char kTouchpadSchema[] = "com.deepin.dde.touchpad";
constexpr char kGestureSchema[] = "com.deepin.dde.gesture";
constexpr char kGestureService[] = "com.deepin.daemon.Gesture";
constexpr char kGesturePath[] = "/com/deepin/daemon/Gesture";
constexpr char kGestureInterface[] = "com.deepin.daemon.Gesture";
constexpr double kSwipeThreshold = 50.0;
constexpr double kPinchThreshold = 0.1;

bool ApplyStatus(libinput_config_status status, const char* setting,
                 const char* device_name) {
  if (status == LIBINPUT_CONFIG_STATUS_SUCCESS) {
    return true;
  }
  ABSL_LOG(WARNING) << "Failed to apply touchpad " << setting << " to "
                    << device_name << ": "
                    << libinput_config_status_to_str(status);
  return false;
}

}  // namespace

struct TouchpadManager::Device {
  Device(TouchpadManager* manager, wlr_input_device* input,
         libinput_device* handle)
      : manager(manager),
        input(input),
        handle(handle),
        destroy(this, OnDestroy) {}

  static void OnDestroy(Device* device, void*) {
    device->destroy.Disconnect();
    device->input = nullptr;
    device->handle = nullptr;
  }

  TouchpadManager* manager;
  wlr_input_device* input;
  libinput_device* handle;
  utils::SignalListener<Device, void> destroy;
};

TouchpadManager::TouchpadManager()
    : gesture_connection_(QDBusConnection::sessionBus()) {
#ifdef FLAKEWM_HAS_QGSETTINGS
  if (QGSettings::isSchemaInstalled(kTouchpadSchema)) {
    settings_source_ = std::make_unique<QGSettings>(kTouchpadSchema);
    QObject::connect(settings_source_.get(), &QGSettings::changed,
                     [this](const QString&) {
                       LoadSettings();
                       ApplySettings();
                     });
  }
  if (QGSettings::isSchemaInstalled(kGestureSchema)) {
    gesture_settings_source_ = std::make_unique<QGSettings>(kGestureSchema);
    gestures_enabled_ =
        gesture_settings_source_->get(QStringLiteral("enabled")).toBool();
    QObject::connect(gesture_settings_source_.get(), &QGSettings::changed,
                     [this](const QString& key) {
                       if (key == QStringLiteral("enabled")) {
                         gestures_enabled_ =
                             gesture_settings_source_->get(key).toBool();
                       }
                     });
  }
#endif
  LoadSettings();
}

TouchpadManager::~TouchpadManager() = default;

void TouchpadManager::AddDevice(wlr_input_device* device) {
  if (device == nullptr || device->type != WLR_INPUT_DEVICE_POINTER ||
      !wlr_input_device_is_libinput(device)) {
    return;
  }
  libinput_device* handle = wlr_libinput_get_device_handle(device);
  if (handle == nullptr ||
      libinput_device_config_tap_get_finger_count(handle) <= 0) {
    return;
  }
  auto touchpad = std::make_unique<Device>(this, device, handle);
  touchpad->destroy.Connect(&device->events.destroy);
  ApplySettings(touchpad.get());
  ABSL_LOG(INFO) << "Configured touchpad " << device->name;
  devices_.push_back(std::move(touchpad));
}

bool TouchpadManager::ShouldForwardAxis(
    const wlr_pointer_axis_event& event) const {
  const Device* device = FindDevice(event.pointer);
  if (device == nullptr || device->handle == nullptr) {
    return true;
  }
  if (libinput_device_config_scroll_get_method(device->handle) ==
      LIBINPUT_CONFIG_SCROLL_EDGE) {
    return true;
  }
  return event.orientation == WL_POINTER_AXIS_HORIZONTAL_SCROLL
             ? settings_.horizontal_scroll
             : settings_.vertical_scroll;
}

void TouchpadManager::BeginSwipe(std::uint32_t fingers) {
  gesture_ = {.type = GestureType::kSwipe, .fingers = fingers};
}

void TouchpadManager::UpdateSwipe(double delta_x, double delta_y) {
  if (gesture_.type != GestureType::kSwipe) {
    return;
  }
  gesture_.delta_x += delta_x;
  gesture_.delta_y += delta_y;
}

bool TouchpadManager::EndSwipe(bool cancelled) {
  if (gesture_.type != GestureType::kSwipe) {
    return false;
  }
  const GestureState gesture = gesture_;
  ResetGesture();
  if (cancelled || gesture.fingers < 3 ||
      std::max(std::abs(gesture.delta_x), std::abs(gesture.delta_y)) <
          kSwipeThreshold) {
    return false;
  }
  const char* direction = nullptr;
  if (std::abs(gesture.delta_x) > std::abs(gesture.delta_y)) {
    direction = gesture.delta_x > 0 ? "right" : "left";
  } else {
    direction = gesture.delta_y > 0 ? "down" : "up";
  }
  return ExecuteGesture("swipe", direction, gesture.fingers);
}

void TouchpadManager::BeginPinch(std::uint32_t fingers) {
  gesture_ = {.type = GestureType::kPinch, .fingers = fingers};
}

void TouchpadManager::UpdatePinch(const wlr_pointer_pinch_update_event& event) {
  if (gesture_.type != GestureType::kPinch) {
    return;
  }
  gesture_.scale = event.scale;
  gesture_.rotation += event.rotation;
}

bool TouchpadManager::EndPinch(bool cancelled) {
  if (gesture_.type != GestureType::kPinch) {
    return false;
  }
  const GestureState gesture = gesture_;
  ResetGesture();
  if (cancelled || gesture.fingers < 3 ||
      std::abs(gesture.scale - 1.0) < kPinchThreshold) {
    return false;
  }
  return ExecuteGesture("pinch", gesture.scale < 1.0 ? "in" : "out",
                        gesture.fingers);
}

void TouchpadManager::BeginHold(std::uint32_t fingers) {
  gesture_ = {.type = GestureType::kHold, .fingers = fingers};
}

bool TouchpadManager::EndHold(bool cancelled) {
  if (gesture_.type != GestureType::kHold) {
    return false;
  }
  const std::uint32_t fingers = gesture_.fingers;
  ResetGesture();
  return !cancelled && fingers >= 3 && ExecuteGesture("tap", "none", fingers);
}

void TouchpadManager::LoadSettings() {
#ifdef FLAKEWM_HAS_QGSETTINGS
  if (settings_source_ == nullptr) {
    return;
  }
  settings_.enabled =
      settings_source_->get(QStringLiteral("touchpadEnabled")).toBool();
  settings_.left_handed =
      settings_source_->get(QStringLiteral("leftHanded")).toBool();
  settings_.disable_while_typing =
      settings_source_->get(QStringLiteral("disableWhileTyping")).toBool();
  settings_.natural_scroll =
      settings_source_->get(QStringLiteral("naturalScroll")).toBool();
  settings_.edge_scroll =
      settings_source_->get(QStringLiteral("edgeScrollEnabled")).toBool();
  settings_.horizontal_scroll =
      settings_source_->get(QStringLiteral("horizScrollEnabled")).toBool();
  settings_.vertical_scroll =
      settings_source_->get(QStringLiteral("vertScrollEnabled")).toBool();
  settings_.tap_to_click =
      settings_source_->get(QStringLiteral("tapToClick")).toBool();
  settings_.acceleration_factor =
      settings_source_->get(QStringLiteral("motionAcceleration")).toDouble();
#endif
}

void TouchpadManager::ApplySettings() {
  for (const std::unique_ptr<Device>& device : devices_) {
    ApplySettings(device.get());
  }
}

void TouchpadManager::ApplySettings(Device* device) {
  if (device == nullptr || device->handle == nullptr) {
    return;
  }
  libinput_device* handle = device->handle;
  const char* name = device->input->name;

  ApplyStatus(
      libinput_device_config_send_events_set_mode(
          handle, settings_.enabled ? LIBINPUT_CONFIG_SEND_EVENTS_ENABLED
                                    : LIBINPUT_CONFIG_SEND_EVENTS_DISABLED),
      "enabled", name);
  ApplyStatus(
      libinput_device_config_tap_set_enabled(
          handle, settings_.tap_to_click ? LIBINPUT_CONFIG_TAP_ENABLED
                                         : LIBINPUT_CONFIG_TAP_DISABLED),
      "tap-to-click", name);
  ApplyStatus(
      libinput_device_config_tap_set_drag_enabled(
          handle, settings_.tap_to_click ? LIBINPUT_CONFIG_DRAG_ENABLED
                                         : LIBINPUT_CONFIG_DRAG_DISABLED),
      "tap-and-drag", name);

  if (libinput_device_config_left_handed_is_available(handle) != 0) {
    ApplyStatus(libinput_device_config_left_handed_set(
                    handle, settings_.left_handed ? 1 : 0),
                "left-handed", name);
  }
  if (libinput_device_config_scroll_has_natural_scroll(handle) != 0) {
    ApplyStatus(libinput_device_config_scroll_set_natural_scroll_enabled(
                    handle, settings_.natural_scroll ? 1 : 0),
                "natural-scroll", name);
  }
  if (libinput_device_config_dwt_is_available(handle) != 0) {
    ApplyStatus(libinput_device_config_dwt_set_enabled(
                    handle, settings_.disable_while_typing
                                ? LIBINPUT_CONFIG_DWT_ENABLED
                                : LIBINPUT_CONFIG_DWT_DISABLED),
                "disable-while-typing", name);
  }
  if (libinput_device_config_accel_is_available(handle) != 0) {
    const double speed =
        std::clamp((settings_.acceleration_factor - 1.0) / 1.8, -1.0, 1.0);
    ApplyStatus(libinput_device_config_accel_set_speed(handle, speed),
                "acceleration", name);
  }

  const std::uint32_t methods =
      libinput_device_config_scroll_get_methods(handle);
  libinput_config_scroll_method method = LIBINPUT_CONFIG_SCROLL_NO_SCROLL;
  if ((settings_.vertical_scroll || settings_.horizontal_scroll) &&
      (methods & LIBINPUT_CONFIG_SCROLL_2FG) != 0) {
    method = LIBINPUT_CONFIG_SCROLL_2FG;
  } else if (settings_.edge_scroll &&
             (methods & LIBINPUT_CONFIG_SCROLL_EDGE) != 0) {
    method = LIBINPUT_CONFIG_SCROLL_EDGE;
  }
  ApplyStatus(libinput_device_config_scroll_set_method(handle, method),
              "scroll-method", name);
}

const TouchpadManager::Device* TouchpadManager::FindDevice(
    const wlr_pointer* pointer) const {
  if (pointer == nullptr) {
    return nullptr;
  }
  auto device = std::find_if(devices_.begin(), devices_.end(),
                             [pointer](const auto& candidate) {
                               return candidate->input == &pointer->base;
                             });
  return device == devices_.end() ? nullptr : device->get();
}

bool TouchpadManager::ExecuteGesture(const char* name, const char* direction,
                                     std::uint32_t fingers) const {
  if (!gestures_enabled_ || !gesture_connection_.isConnected()) {
    return false;
  }
  QDBusConnectionInterface* interface = gesture_connection_.interface();
  if (interface == nullptr ||
      !interface->isServiceRegistered(QString::fromLatin1(kGestureService))) {
    return false;
  }
  QDBusMessage message = QDBusMessage::createMethodCall(
      QString::fromLatin1(kGestureService), QString::fromLatin1(kGesturePath),
      QString::fromLatin1(kGestureInterface), QStringLiteral("Exec"));
  message << QString::fromLatin1(name) << QString::fromLatin1(direction)
          << static_cast<std::int32_t>(fingers);
  return gesture_connection_.send(message);
}

void TouchpadManager::ResetGesture() { gesture_ = {}; }

}  // namespace input
}  // namespace flakewm
