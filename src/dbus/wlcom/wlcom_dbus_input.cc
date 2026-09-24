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

#include "src/dbus/wlcom/wlcom_dbus_manager_p.h"

namespace flakewm {
namespace dbus {

void WlcomDbusManager::AddInput(wlr_input_device* device) {
  if (device == nullptr) return;
  auto input = std::make_unique<InputDevice>(this, device);
  libinput_device* libinput = nullptr;
  if (wlr_input_device_is_libinput(device)) {
    libinput = wlr_libinput_get_device_handle(device);
    if (libinput != nullptr)
      input->sys_name =
          QString::fromUtf8(libinput_device_get_sysname(libinput));
  }
  if (input->sys_name.isEmpty())
    input->sys_name = QStringLiteral("flakewm-%1")
                          .arg(reinterpret_cast<quintptr>(device), 0, 16);
  QString device_name =
      QString::fromUtf8(device->name == nullptr ? "" : device->name).trimmed();
  for (QChar& character : device_name)
    if (character.isSpace() || !character.isPrint())
      character = QLatin1Char('_');
  const unsigned int vendor =
      libinput == nullptr ? 0 : libinput_device_get_id_vendor(libinput);
  const unsigned int product =
      libinput == nullptr ? 0 : libinput_device_get_id_product(libinput);

  // Packed aligning GXWM's input_prop union. The value is uint32.
  quint32 properties = 0;
  if (libinput != nullptr) {
    properties |= static_cast<quint32>(
                      libinput_device_config_tap_get_finger_count(libinput)) &
                  0x7U;
    properties |= (static_cast<quint32>(
                       libinput_device_config_click_get_methods(libinput)) &
                   0x7U)
                  << 3U;
    properties |= (static_cast<quint32>(
                       libinput_device_config_scroll_get_methods(libinput)) &
                   0x7U)
                  << 6U;
    properties |= (static_cast<quint32>(
                       libinput_device_config_accel_get_profiles(libinput)) &
                   0x7U)
                  << 9U;
    properties |= (static_cast<quint32>(
                       libinput_device_config_send_events_get_modes(libinput)) &
                   0x7U)
                  << 12U;
    properties |=
        static_cast<quint32>(
            libinput_device_config_rotation_is_available(libinput) != 0)
        << 15U;
    properties |= static_cast<quint32>(
                      libinput_device_config_dwtp_is_available(libinput) != 0)
                  << 16U;
    properties |= static_cast<quint32>(
                      libinput_device_config_dwt_is_available(libinput) != 0)
                  << 17U;
    properties |=
        static_cast<quint32>(
            libinput_device_config_middle_emulation_is_available(libinput) != 0)
        << 18U;
    properties |=
        static_cast<quint32>(
            libinput_device_config_left_handed_is_available(libinput) != 0)
        << 19U;
    properties |=
        static_cast<quint32>(
            libinput_device_config_scroll_has_natural_scroll(libinput) != 0)
        << 20U;
    properties |=
        static_cast<quint32>(
            libinput_device_config_calibration_has_matrix(libinput) != 0)
        << 21U;
    properties |= static_cast<quint32>(
                      libinput_device_config_accel_is_available(libinput) != 0)
                  << 22U;
  }
  if (device->type == WLR_INPUT_DEVICE_POINTER ||
      device->type == WLR_INPUT_DEVICE_TOUCH ||
      device->type == WLR_INPUT_DEVICE_TABLET)
    properties |= 1U << 23U;
  const QString base_name = QStringLiteral("%1:%2:%3:%4")
                                .arg(static_cast<int>(device->type))
                                .arg(vendor)
                                .arg(product)
                                .arg(device_name);
  input->name = base_name;
  input->properties = properties;
  int suffix = 0;
  while (std::any_of(inputs_.begin(), inputs_.end(), [&](const auto& item) {
    return item->name == input->name;
  })) {
    input->name = base_name + QStringLiteral("_%1").arg(++suffix);
  }
  const QString sys_name = input->sys_name;
  input->destroy.Connect(&device->events.destroy);
  if (device->type == WLR_INPUT_DEVICE_KEYBOARD) {
    wlr_keyboard* keyboard = wlr_keyboard_from_input_device(device);
    input->keymap_group = keyboard->modifiers.group;
    input->modifiers.Connect(&keyboard->events.modifiers);
  }
  inputs_.push_back(std::move(input));
  ApplyInputConfig(inputs_.back().get());
  EmitSignal(QStringLiteral("/com/kylin/Wlcom/Input"),
             QStringLiteral("com.kylin.Wlcom.Input"),
             QStringLiteral("input_create"),
             {inputs_.back()->name, inputs_.back()->properties});
  EmitSignal(QStringLiteral("/org/kde/KWin/InputDevice"),
             QStringLiteral("org.kde.KWin.InputDeviceManager"),
             QStringLiteral("deviceAdded"), {sys_name});
}

void WlcomDbusManager::RemoveInput(InputDevice* input) {
  if (input == nullptr) return;
  const QString sys_name = input->sys_name;
  const QString name = input->name;
  std::erase_if(inputs_,
                [input](const auto& item) { return item.get() == input; });
  EmitSignal(QStringLiteral("/org/kde/KWin/InputDevice"),
             QStringLiteral("org.kde.KWin.InputDeviceManager"),
             QStringLiteral("deviceRemoved"), {sys_name});
  EmitSignal(QStringLiteral("/com/kylin/Wlcom/Input"),
             QStringLiteral("com.kylin.Wlcom.Input"),
             QStringLiteral("input_destroy"), {name});
}

WlcomDbusManager::InputDevice* WlcomDbusManager::FindInput(
    const QString& name) const {
  for (const auto& input : inputs_) {
    if (input->device != nullptr &&
        (input->name == name || input->sys_name == name ||
         QString::fromUtf8(input->device->name) == name))
      return input.get();
  }
  return nullptr;
}

void WlcomDbusManager::ApplyInputConfig(InputDevice* input) {
  if (input == nullptr || input->device == nullptr) return;
  const QJsonObject state = config_.value(QStringLiteral("Inputs"))
                                .toObject()
                                .value(input->name)
                                .toObject();
  if (state.isEmpty()) return;

  if (libinput_device* device = Libinput(input->device); device != nullptr) {
    if (state.contains(QStringLiteral("send_events_mode")))
      libinput_device_config_send_events_set_mode(
          device, static_cast<libinput_config_send_events_mode>(
                      state.value(QStringLiteral("send_events_mode")).toInt()));
    if (state.contains(QStringLiteral("tap_to_click")))
      libinput_device_config_tap_set_enabled(
          device, state.value(QStringLiteral("tap_to_click")).toBool()
                      ? LIBINPUT_CONFIG_TAP_ENABLED
                      : LIBINPUT_CONFIG_TAP_DISABLED);
    if (state.contains(QStringLiteral("tap_and_drag")))
      libinput_device_config_tap_set_drag_enabled(
          device, state.value(QStringLiteral("tap_and_drag")).toBool()
                      ? LIBINPUT_CONFIG_DRAG_ENABLED
                      : LIBINPUT_CONFIG_DRAG_DISABLED);
    if (state.contains(QStringLiteral("pointer_accel_speed")))
      libinput_device_config_accel_set_speed(
          device,
          state.value(QStringLiteral("pointer_accel_speed")).toDouble());
    if (state.contains(QStringLiteral("accel_profile")))
      libinput_device_config_accel_set_profile(
          device, static_cast<libinput_config_accel_profile>(
                      state.value(QStringLiteral("accel_profile")).toInt()));
    if (state.contains(QStringLiteral("scroll_method")))
      libinput_device_config_scroll_set_method(
          device, static_cast<libinput_config_scroll_method>(
                      state.value(QStringLiteral("scroll_method")).toInt()));
    if (state.contains(QStringLiteral("dwt")))
      libinput_device_config_dwt_set_enabled(
          device, state.value(QStringLiteral("dwt")).toBool()
                      ? LIBINPUT_CONFIG_DWT_ENABLED
                      : LIBINPUT_CONFIG_DWT_DISABLED);
    if (state.contains(QStringLiteral("natural_scroll")))
      libinput_device_config_scroll_set_natural_scroll_enabled(
          device, state.value(QStringLiteral("natural_scroll")).toBool());
    if (state.contains(QStringLiteral("left_handed")))
      libinput_device_config_left_handed_set(
          device, state.value(QStringLiteral("left_handed")).toBool());
  }

  if (input->device->type == WLR_INPUT_DEVICE_KEYBOARD) {
    wlr_keyboard* keyboard = wlr_keyboard_from_input_device(input->device);
    if (state.contains(QStringLiteral("repeat_rate")) ||
        state.contains(QStringLiteral("repeat_delay"))) {
      wlr_keyboard_set_repeat_info(keyboard,
                                   state.value(QStringLiteral("repeat_rate"))
                                       .toInt(keyboard->repeat_info.rate),
                                   state.value(QStringLiteral("repeat_delay"))
                                       .toInt(keyboard->repeat_info.delay));
    }
    if (state.contains(QStringLiteral("xkb_layout"))) {
      const QByteArray rules = state.value("xkb_rules").toString().toUtf8();
      const QByteArray model = state.value("xkb_model").toString().toUtf8();
      const QByteArray layout = state.value("xkb_layout").toString().toUtf8();
      const QByteArray variant = state.value("xkb_variant").toString().toUtf8();
      const QByteArray options = state.value("xkb_options").toString().toUtf8();
      xkb_rule_names names{rules.isEmpty() ? nullptr : rules.constData(),
                           model.isEmpty() ? nullptr : model.constData(),
                           layout.isEmpty() ? nullptr : layout.constData(),
                           variant.isEmpty() ? nullptr : variant.constData(),
                           options.isEmpty() ? nullptr : options.constData()};
      xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
      xkb_keymap* keymap =
          context == nullptr
              ? nullptr
              : xkb_keymap_new_from_names(context, &names,
                                          XKB_KEYMAP_COMPILE_NO_FLAGS);
      if (keymap != nullptr) {
        wlr_keyboard_set_keymap(keyboard, keymap);
        xkb_keymap_unref(keymap);
      }
      if (context != nullptr) xkb_context_unref(context);
    }
  }

  if (state.contains(QStringLiteral("mapped_to_output"))) {
    wlr_cursor_map_input_to_output(
        compositor_->cursor_, input->device,
        FindOutput(state.value(QStringLiteral("mapped_to_output")).toString()));
  }
}

bool WlcomDbusManager::HandleInput(const QDBusMessage& message) {
  const QVariantList args = message.arguments();
  if (message.member() == QStringLiteral("ListAllInputs")) {
    QList<types::Input> result;
    for (const auto& input : inputs_) {
      if (input->device == nullptr) continue;
      result << types::Input{input->name, input->properties};
    }
    Reply(message, {DbusArray(result)});
    return true;
  }
  InputDevice* input = FindInput(args.value(0).toString());
  auto invalid = [&]() {
    Error(message, kInvalidArgs, "Invalid input device or value.");
  };
  if (message.member() != QStringLiteral("ListAllInputs") && input == nullptr) {
    invalid();
    return true;
  }
  libinput_device* libinput =
      Libinput(input == nullptr ? nullptr : input->device);
  QJsonObject input_configs =
      config_.value(QStringLiteral("Inputs")).toObject();
  QJsonObject input_config = input_configs.value(input->name).toObject();
  auto persist_input = [&](const QString& key, const QJsonValue& value) {
    if (value.isUndefined())
      input_config.remove(key);
    else
      input_config[key] = value;
    input_configs[input->name] = input_config;
    config_[QStringLiteral("Inputs")] = input_configs;
    SaveConfig();
  };
  if (message.member() == QStringLiteral("MapToOutput")) {
    const bool none = args.value(1).toString() == QStringLiteral("none");
    wlr_output* output = none ? nullptr : FindOutput(args.value(1).toString());
    if (args.size() != 2 || (!none && output == nullptr))
      invalid();
    else {
      wlr_cursor_map_input_to_output(compositor_->cursor_, input->device,
                                     output);
      persist_input(QStringLiteral("mapped_to_output"),
                    none ? QJsonValue(QJsonValue::Undefined)
                         : QJsonValue(args[1].toString()));
      Reply(message);
    }
    return true;
  }
  if (message.member() == QStringLiteral("ChangeSeat")) {
    if (args.size() != 2 || !args[1].toString().startsWith("seat"))
      invalid();
    else {
      persist_input(QStringLiteral("seat"), args[1].toString());
      Reply(message);
    }
    return true;
  }
  if (libinput == nullptr &&
      !message.member().contains(QStringLiteral("Repeat")) &&
      !message.member().contains(QStringLiteral("Keymap")) &&
      !message.member().contains(QStringLiteral("ScrollFactor")) &&
      !message.member().contains(QStringLiteral("DoubleClick"))) {
    invalid();
    return true;
  }
  if (message.member() == QStringLiteral("GetSendEventsMode")) {
    Reply(message,
          {static_cast<quint32>(
               libinput_device_config_send_events_get_mode(libinput)),
           static_cast<quint32>(
               libinput_device_config_send_events_get_default_mode(libinput))});
    return true;
  }
  if (message.member() == QStringLiteral("SetSendEventsMode")) {
    const auto status = libinput_device_config_send_events_set_mode(
        libinput,
        static_cast<libinput_config_send_events_mode>(args.value(1).toUInt()));
    if (status != LIBINPUT_CONFIG_STATUS_SUCCESS)
      invalid();
    else {
      persist_input(QStringLiteral("send_events_mode"),
                    static_cast<int>(args.value(1).toUInt()));
      Reply(message);
    }
    return true;
  }
  if (message.member() == QStringLiteral("GetTapToClick")) {
    Reply(message, {libinput_device_config_tap_get_enabled(libinput) ==
                        LIBINPUT_CONFIG_TAP_ENABLED,
                    libinput_device_config_tap_get_default_enabled(libinput) ==
                        LIBINPUT_CONFIG_TAP_ENABLED});
    return true;
  }
  if (message.member() == QStringLiteral("EnableTapToClick")) {
    const auto status = libinput_device_config_tap_set_enabled(
        libinput, args.value(1).toBool() ? LIBINPUT_CONFIG_TAP_ENABLED
                                         : LIBINPUT_CONFIG_TAP_DISABLED);
    if (status != LIBINPUT_CONFIG_STATUS_SUCCESS)
      invalid();
    else {
      persist_input(QStringLiteral("tap_to_click"), args.value(1).toBool());
      Reply(message);
    }
    return true;
  }
  if (message.member() == QStringLiteral("GetTapAndDrag")) {
    Reply(message, {libinput_device_config_tap_get_drag_enabled(libinput) ==
                        LIBINPUT_CONFIG_DRAG_ENABLED,
                    libinput_device_config_tap_get_default_drag_enabled(
                        libinput) == LIBINPUT_CONFIG_DRAG_ENABLED});
    return true;
  }
  if (message.member() == QStringLiteral("EnableTapAndDrag")) {
    const auto status = libinput_device_config_tap_set_drag_enabled(
        libinput, args.value(1).toBool() ? LIBINPUT_CONFIG_DRAG_ENABLED
                                         : LIBINPUT_CONFIG_DRAG_DISABLED);
    if (status != LIBINPUT_CONFIG_STATUS_SUCCESS)
      invalid();
    else {
      persist_input(QStringLiteral("tap_and_drag"), args.value(1).toBool());
      Reply(message);
    }
    return true;
  }
  if (message.member() == QStringLiteral("GetPointerSpeed")) {
    Reply(message, {libinput_device_config_accel_get_speed(libinput),
                    libinput_device_config_accel_get_default_speed(libinput)});
    return true;
  }
  if (message.member() == QStringLiteral("SetPointerSpeed")) {
    const double speed = args.value(1).toDouble();
    if (speed < -1.0 || speed > 1.0 ||
        libinput_device_config_accel_set_speed(libinput, speed) !=
            LIBINPUT_CONFIG_STATUS_SUCCESS)
      invalid();
    else {
      persist_input(QStringLiteral("pointer_accel_speed"), speed);
      Reply(message);
    }
    return true;
  }
  if (message.member() == QStringLiteral("GetAccelProfile")) {
    Reply(message,
          {static_cast<quint32>(
               libinput_device_config_accel_get_profile(libinput)),
           static_cast<quint32>(
               libinput_device_config_accel_get_default_profile(libinput))});
    return true;
  }
  if (message.member() == QStringLiteral("SetAccelProfile")) {
    const auto status = libinput_device_config_accel_set_profile(
        libinput,
        static_cast<libinput_config_accel_profile>(args.value(1).toUInt()));
    if (status != LIBINPUT_CONFIG_STATUS_SUCCESS)
      invalid();
    else {
      persist_input(QStringLiteral("accel_profile"),
                    static_cast<int>(args.value(1).toUInt()));
      Reply(message);
    }
    return true;
  }
  if (message.member() == QStringLiteral("GetScrollMethod")) {
    Reply(message,
          {static_cast<quint32>(
               libinput_device_config_scroll_get_method(libinput)),
           static_cast<quint32>(
               libinput_device_config_scroll_get_default_method(libinput))});
    return true;
  }
  if (message.member() == QStringLiteral("SetScrollMethod")) {
    const auto status = libinput_device_config_scroll_set_method(
        libinput,
        static_cast<libinput_config_scroll_method>(args.value(1).toUInt()));
    if (status != LIBINPUT_CONFIG_STATUS_SUCCESS)
      invalid();
    else {
      persist_input(QStringLiteral("scroll_method"),
                    static_cast<int>(args.value(1).toUInt()));
      Reply(message);
    }
    return true;
  }
  if (message.member() == QStringLiteral("GetDisableWhileTyping")) {
    Reply(message, {libinput_device_config_dwt_get_enabled(libinput) ==
                        LIBINPUT_CONFIG_DWT_ENABLED,
                    libinput_device_config_dwt_get_default_enabled(libinput) ==
                        LIBINPUT_CONFIG_DWT_ENABLED});
    return true;
  }
  if (message.member() == QStringLiteral("SetDisableWhileTyping")) {
    const auto status = libinput_device_config_dwt_set_enabled(
        libinput, args.value(1).toBool() ? LIBINPUT_CONFIG_DWT_ENABLED
                                         : LIBINPUT_CONFIG_DWT_DISABLED);
    if (status != LIBINPUT_CONFIG_STATUS_SUCCESS)
      invalid();
    else {
      persist_input(QStringLiteral("dwt"), args.value(1).toBool());
      Reply(message);
    }
    return true;
  }
  if (message.member() == QStringLiteral("GetNaturalScroll")) {
    Reply(message,
          {libinput_device_config_scroll_get_natural_scroll_enabled(libinput) !=
               0,
           libinput_device_config_scroll_get_default_natural_scroll_enabled(
               libinput) != 0});
    return true;
  }
  if (message.member() == QStringLiteral("EnableNaturalScroll")) {
    const auto status =
        libinput_device_config_scroll_set_natural_scroll_enabled(
            libinput, args.value(1).toBool());
    if (status != LIBINPUT_CONFIG_STATUS_SUCCESS)
      invalid();
    else {
      persist_input(QStringLiteral("natural_scroll"), args.value(1).toBool());
      Reply(message);
    }
    return true;
  }
  if (message.member() == QStringLiteral("GetLeftHand")) {
    Reply(message,
          {libinput_device_config_left_handed_get(libinput) != 0,
           libinput_device_config_left_handed_get_default(libinput) != 0});
    return true;
  }
  if (message.member() == QStringLiteral("EnableLeftHand")) {
    const auto status = libinput_device_config_left_handed_set(
        libinput, args.value(1).toBool());
    if (status != LIBINPUT_CONFIG_STATUS_SUCCESS)
      invalid();
    else {
      persist_input(QStringLiteral("left_handed"), args.value(1).toBool());
      Reply(message);
    }
    return true;
  }
  wlr_keyboard* keyboard = input->device->type == WLR_INPUT_DEVICE_KEYBOARD
                               ? wlr_keyboard_from_input_device(input->device)
                               : nullptr;
  if (message.member() == QStringLiteral("GetRepeatInfo")) {
    if (keyboard == nullptr)
      invalid();
    else
      Reply(message,
            {keyboard->repeat_info.rate, keyboard->repeat_info.delay, 25, 600});
    return true;
  }
  if (message.member() == QStringLiteral("SetRepeatInfo")) {
    if (keyboard == nullptr || args.size() != 3 || args[1].toInt() < 0 ||
        args[2].toInt() < 0)
      invalid();
    else {
      wlr_keyboard_set_repeat_info(keyboard, args[1].toInt(), args[2].toInt());
      persist_input(QStringLiteral("repeat_rate"), args[1].toInt());
      persist_input(QStringLiteral("repeat_delay"), args[2].toInt());
      Reply(message);
    }
    return true;
  }
  if (message.member() == QStringLiteral("GetKeymap")) {
    Reply(message,
          {input_config.value("xkb_rules").toString(),
           input_config.value("xkb_model").toString(),
           input_config.value("xkb_layout").toString(QStringLiteral("us")),
           input_config.value("xkb_variant").toString(),
           input_config.value("xkb_options").toString()});
    return true;
  }
  if (message.member() == QStringLiteral("SetKeymap")) {
    if (keyboard == nullptr || args.size() != 6) {
      invalid();
      return true;
    }
    const QByteArray rules = args[1].toString().toUtf8(),
                     model = args[2].toString().toUtf8();
    const QByteArray layout = args[3].toString().toUtf8(),
                     variant = args[4].toString().toUtf8();
    const QByteArray options = args[5].toString().toUtf8();
    xkb_rule_names names{rules.constData(), model.constData(),
                         layout.constData(), variant.constData(),
                         options.constData()};
    xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    xkb_keymap* keymap =
        context == nullptr ? nullptr
                           : xkb_keymap_new_from_names(
                                 context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (keymap == nullptr) {
      if (context) xkb_context_unref(context);
      invalid();
      return true;
    }
    wlr_keyboard_set_keymap(keyboard, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);
    input_config[QStringLiteral("xkb_rules")] = args[1].toString();
    input_config[QStringLiteral("xkb_model")] = args[2].toString();
    input_config[QStringLiteral("xkb_layout")] = args[3].toString();
    input_config[QStringLiteral("xkb_variant")] = args[4].toString();
    persist_input(QStringLiteral("xkb_options"), args[5].toString());
    Reply(message);
    return true;
  }
  if (message.member() == QStringLiteral("GetKeymapGroup")) {
    if (keyboard == nullptr)
      invalid();
    else
      Reply(message, {keyboard->modifiers.group});
    return true;
  }
  if (message.member() == QStringLiteral("SetKeymapGroup")) {
    const quint32 group = args.value(1).toUInt();
    if (keyboard == nullptr || keyboard->keymap == nullptr ||
        group >= xkb_keymap_num_layouts(keyboard->keymap)) {
      invalid();
    } else {
      xkb_state_update_mask(keyboard->xkb_state, keyboard->modifiers.depressed,
                            keyboard->modifiers.latched,
                            keyboard->modifiers.locked, 0, 0, group);
      keyboard->modifiers.depressed = xkb_state_serialize_mods(
          keyboard->xkb_state, XKB_STATE_MODS_DEPRESSED);
      keyboard->modifiers.latched =
          xkb_state_serialize_mods(keyboard->xkb_state, XKB_STATE_MODS_LATCHED);
      keyboard->modifiers.locked =
          xkb_state_serialize_mods(keyboard->xkb_state, XKB_STATE_MODS_LOCKED);
      keyboard->modifiers.group = xkb_state_serialize_layout(
          keyboard->xkb_state, XKB_STATE_LAYOUT_EFFECTIVE);
      wl_signal_emit_mutable(&keyboard->events.modifiers, keyboard);
      Reply(message);
    }
    return true;
  }
  if (message.member() == QStringLiteral("GetScrollFactor")) {
    const double value =
        input_config.value(QStringLiteral("scroll_factor")).toDouble(1.0);
    Reply(message, {value, 1.0});
    return true;
  }
  if (message.member() == QStringLiteral("SetScrollFactor")) {
    persist_input(QStringLiteral("scroll_factor"), args.value(1).toDouble());
    Reply(message);
    return true;
  }
  if (message.member() == QStringLiteral("GetDoubleClickTime")) {
    const quint32 value = static_cast<quint32>(
        input_config.value(QStringLiteral("double_click_time")).toInt(400));
    Reply(message, {value, static_cast<quint32>(400)});
    return true;
  }
  if (message.member() == QStringLiteral("SetDoubleClickTime")) {
    persist_input(QStringLiteral("double_click_time"),
                  static_cast<int>(args.value(1).toUInt()));
    Reply(message);
    return true;
  }
  return false;
}

bool WlcomDbusManager::HandleSeat(const QDBusMessage& message) {
  const QVariantList args = message.arguments();
  QJsonObject seats = config_.value(QStringLiteral("Seats")).toObject();
  QJsonObject seat = seats.value(QStringLiteral("seat0")).toObject();
  auto persist_seat = [&]() {
    seats[QStringLiteral("seat0")] = seat;
    config_[QStringLiteral("Seats")] = seats;
    SaveConfig();
  };
  if (message.member() == QStringLiteral("ListAllSeats")) {
    Reply(message, {DbusArray(QList<types::StringPair>{
                       {QStringLiteral("seat0"),
                        QString::fromUtf8(QJsonDocument(seat).toJson(
                            QJsonDocument::Compact))}})});
    return true;
  }
  if (args.value(0).toString() != QStringLiteral("seat0")) {
    Error(message, kInvalidArgs, "Invalid seat.");
    return true;
  }
  if (message.member() == QStringLiteral("SetCursor")) {
    if (args.size() != 3 || args[2].toUInt() == 0) {
      Error(message, kInvalidArgs, "Invalid cursor.");
      return true;
    }
    const QByteArray theme = args[1].toString().toUtf8();
    wlr_xcursor_manager* manager = wlr_xcursor_manager_create(
        theme.isEmpty() ? nullptr : theme.constData(), args[2].toUInt());
    if (manager == nullptr || !wlr_xcursor_manager_load(manager, 1.0F)) {
      if (manager != nullptr) wlr_xcursor_manager_destroy(manager);
      Error(message, kFailed, "Failed to load cursor theme.");
      return true;
    }
    wlr_xcursor_manager_destroy(compositor_->cursor_manager_);
    compositor_->cursor_manager_ = manager;
    compositor_->SetCursorName("default");
    if (compositor_->shake_cursor_ != nullptr)
      compositor_->shake_cursor_->ReloadImage();
    seat[QStringLiteral("cursor_theme")] = args[1].toString();
    seat[QStringLiteral("cursor_size")] = static_cast<int>(args[2].toUInt());
    persist_seat();
    Reply(message);
    return true;
  }
  if (message.member() == QStringLiteral("SetLockKeysMode")) {
    seat[QStringLiteral("keyboard_lock_mode")] =
        static_cast<int>(args.value(1).toUInt());
    persist_seat();
    Reply(message);
    return true;
  }
  return false;
}

void WlcomDbusManager::ExecuteInputAction(const QJsonObject& action) {
  const QString type = action.value(QStringLiteral("actiontype")).toString();
  if (type == QStringLiteral("command")) {
    const QString command = action.value(QStringLiteral("command")).toString();
    if (!command.isEmpty())
      QProcess::startDetached(QStringLiteral("/bin/sh"),
                              {QStringLiteral("-c"), command});
    return;
  }
  if (type == QStringLiteral("dbus")) {
    QDBusMessage call = QDBusMessage::createMethodCall(
        action.value(QStringLiteral("service")).toString(),
        action.value(QStringLiteral("path")).toString(),
        action.value(QStringLiteral("interface")).toString(),
        action.value(QStringLiteral("method")).toString());
    (action.value(QStringLiteral("bustype")).toString() ==
             QStringLiteral("system")
         ? system_bus_
         : session_bus_)
        .send(call);
    return;
  }
  if (type == QStringLiteral("button") && compositor_ != nullptr &&
      compositor_->seat_ != nullptr) {
    static const QHash<QString, uint32_t> buttons = {
        {QStringLiteral("left"), BTN_LEFT},
        {QStringLiteral("right"), BTN_RIGHT},
        {QStringLiteral("middle"), BTN_MIDDLE},
        {QStringLiteral("back"), BTN_BACK},
        {QStringLiteral("forward"), BTN_FORWARD}};
    const uint32_t button =
        buttons.value(action.value(QStringLiteral("button")).toString());
    if (button != 0) {
      const uint32_t time = MonotonicTimeMsec();
      wlr_seat_pointer_notify_button(compositor_->seat_, time, button,
                                     WL_POINTER_BUTTON_STATE_PRESSED);
      wlr_seat_pointer_notify_button(compositor_->seat_, time, button,
                                     WL_POINTER_BUTTON_STATE_RELEASED);
      wlr_seat_pointer_notify_frame(compositor_->seat_);
    }
    return;
  }
  if (type == QStringLiteral("key") && compositor_ != nullptr &&
      compositor_->seat_ != nullptr) {
    // Keep the accepted key names and press/release ordering byte-for-byte
    // compatible with gxde-wlcom's input/action.c.  In particular, an omitted
    // suffix means a click and an explicit :press/:release is one-sided.
    static const QHash<QString, uint32_t> keycodes = {
        {QStringLiteral("a"), KEY_A},
        {QStringLiteral("b"), KEY_B},
        {QStringLiteral("c"), KEY_C},
        {QStringLiteral("d"), KEY_D},
        {QStringLiteral("e"), KEY_E},
        {QStringLiteral("f"), KEY_F},
        {QStringLiteral("g"), KEY_G},
        {QStringLiteral("h"), KEY_H},
        {QStringLiteral("i"), KEY_I},
        {QStringLiteral("k"), KEY_K},
        {QStringLiteral("l"), KEY_L},
        {QStringLiteral("m"), KEY_M},
        {QStringLiteral("n"), KEY_N},
        {QStringLiteral("o"), KEY_O},
        {QStringLiteral("p"), KEY_P},
        {QStringLiteral("q"), KEY_Q},
        {QStringLiteral("r"), KEY_R},
        {QStringLiteral("s"), KEY_S},
        {QStringLiteral("t"), KEY_T},
        {QStringLiteral("u"), KEY_U},
        {QStringLiteral("v"), KEY_V},
        {QStringLiteral("w"), KEY_W},
        {QStringLiteral("x"), KEY_X},
        {QStringLiteral("y"), KEY_Y},
        {QStringLiteral("z"), KEY_Z},
        {QStringLiteral("tab"), KEY_TAB},
        {QStringLiteral("super_l"), KEY_LEFTMETA},
        {QStringLiteral("alt_l"), KEY_LEFTALT},
        {QStringLiteral("left"), KEY_LEFT},
        {QStringLiteral("right"), KEY_RIGHT},
        {QStringLiteral("down"), KEY_DOWN},
        {QStringLiteral("up"), KEY_UP},
        {QStringLiteral("shift_l"), KEY_LEFTSHIFT},
        {QStringLiteral("control_r"), KEY_RIGHTCTRL},
        {QStringLiteral("control_l"), KEY_LEFTCTRL},
        {QStringLiteral("alt_r"), KEY_RIGHTALT},
        {QStringLiteral("super_r"), KEY_RIGHTMETA},
        {QStringLiteral("shift_r"), KEY_RIGHTSHIFT}};
    struct KeyList {
      QList<uint32_t> codes;
      bool press = true;
      bool release = true;
    };
    auto parse = [&](const QString& raw) {
      KeyList result;
      const QStringList action_parts = raw.split(QLatin1Char(':'));
      if (action_parts.size() == 2) {
        if (action_parts[1] == QStringLiteral("press")) {
          result.release = false;
        } else if (action_parts[1] == QStringLiteral("release")) {
          result.press = false;
        }
      }
      for (const QString& name :
           action_parts.value(0).split(QLatin1Char('+'))) {
        const uint32_t code = keycodes.value(name.toLower());
        if (code != 0) result.codes.push_back(code);
      }
      return result;
    };
    const KeyList modifiers =
        parse(action.value(QStringLiteral("modifiers")).toString());
    const KeyList keys = parse(action.value(QStringLiteral("keys")).toString());
    wlr_keyboard* keyboard = nullptr;
    for (const auto& candidate : compositor_->keyboards_) {
      if (candidate != nullptr && candidate->handle != nullptr) {
        keyboard = candidate->handle;
        break;
      }
    }
    auto send = [&](uint32_t code, bool pressed) {
      const wl_keyboard_key_state state = pressed
                                              ? WL_KEYBOARD_KEY_STATE_PRESSED
                                              : WL_KEYBOARD_KEY_STATE_RELEASED;
      if (keyboard != nullptr) {
        wlr_keyboard_key_event event = {.time_msec = MonotonicTimeMsec(),
                                        .keycode = code,
                                        .update_state = true,
                                        .state = state};
        wlr_keyboard_notify_key(keyboard, &event);
      } else {
        wlr_seat_keyboard_notify_key(compositor_->seat_, MonotonicTimeMsec(),
                                     code, state);
      }
    };
    if (modifiers.press)
      for (uint32_t code : modifiers.codes) send(code, true);
    if (keys.press)
      for (uint32_t code : keys.codes) send(code, true);
    if (modifiers.release)
      for (uint32_t code : modifiers.codes) send(code, false);
    if (keys.release)
      for (uint32_t code : keys.codes) send(code, false);
  }
}

void WlcomDbusManager::LoadInputActions() {
  if (key_bindings_ == nullptr) return;
  QJsonObject actions = system_config_.value(QStringLiteral("InputAction"))
                            .toObject()
                            .value(QStringLiteral("keyboard"))
                            .toObject();
  const QJsonObject user_actions = config_.value(QStringLiteral("InputAction"))
                                       .toObject()
                                       .value(QStringLiteral("keyboard"))
                                       .toObject();
  for (auto it = user_actions.begin(); it != user_actions.end(); ++it)
    actions[it.key()] = it.value();
  for (auto it = actions.begin(); it != actions.end(); ++it) {
    const QJsonObject action = it.value().toObject();
    if (!action.value(QStringLiteral("enable")).toBool()) continue;
    const auto id = key_bindings_->Register(
        it.key().toStdString(), input::KeyBindingType::kCustom,
        [this, action]() { ExecuteInputAction(action); },
        action.value(QStringLiteral("desc")).toString().toStdString());
    if (id.has_value()) custom_binding_ids_[it.key()] = *id;
  }
}

void WlcomDbusManager::SetupUkuiShortcutIntegration() {
  QDBusConnectionInterface* bus = session_bus_.interface();
  if (bus == nullptr) return;
  QObject::connect(
      bus, &QDBusConnectionInterface::serviceRegistered, this,
      [this](const QString& name) {
        if (name.startsWith(QStringLiteral("org.ukui.settingsDaemon")))
          AddUkuiShortcutService(name);
      });
  QObject::connect(
      bus, &QDBusConnectionInterface::serviceUnregistered, this,
      [this](const QString& name) {
        if (name.startsWith(QStringLiteral("org.ukui.settingsDaemon")))
          RemoveUkuiShortcutService(name);
      });
  const QDBusReply<QStringList> names = bus->registeredServiceNames();
  if (!names.isValid()) return;
  for (const QString& name : names.value()) {
    if (name.startsWith(QStringLiteral("org.ukui.settingsDaemon")))
      AddUkuiShortcutService(name);
  }
}

void WlcomDbusManager::AddUkuiShortcutService(const QString& name) {
  if (ukui_shortcut_services_.contains(name)) return;
  ukui_shortcut_services_.insert(name);
  for (const auto& request :
       {std::pair{QStringLiteral("blockShortcuts"), false},
        std::pair{QStringLiteral("unblockShortcuts"), true}}) {
    QDBusMessage call = QDBusMessage::createMethodCall(
        name, QStringLiteral("/org/ukui/settingsDaemon/shortcut"),
        QStringLiteral("org.ukui.settingsDaemon.shortcut"), request.first);
    auto* watcher =
        new QDBusPendingCallWatcher(session_bus_.asyncCall(call), this);
    QObject::connect(
        watcher, &QDBusPendingCallWatcher::finished, this,
        [this, name,
         whitelist = request.second](QDBusPendingCallWatcher* done) {
          QDBusPendingReply<QStringList> reply = *done;
          if (reply.isValid() && ukui_shortcut_services_.contains(name))
            ApplyUkuiShortcutTypes(name, reply.value(), whitelist);
          done->deleteLater();
        });
  }
}

void WlcomDbusManager::ApplyUkuiShortcutTypes(const QString& service,
                                              const QStringList& types,
                                              bool whitelist) {
  if (key_bindings_ == nullptr) return;
  auto named_type =
      [](const QString& name) -> std::optional<input::KeyBindingType> {
    for (std::size_t index = 0;
         index < static_cast<std::size_t>(input::KeyBindingType::kCount);
         ++index) {
      const auto type = static_cast<input::KeyBindingType>(index);
      const char* type_name = input::KeyBindingManager::TypeName(type);
      if (type_name != nullptr && name == QString::fromLatin1(type_name))
        return type;
    }
    return std::nullopt;
  };
  if (!whitelist) {
    ukui_shortcut_blacklists_[service] = types;
    if (types.contains(QStringLiteral("WLCOM_ALL"))) {
      key_bindings_->BlockAll(true);
      ukui_shortcut_block_all_.insert(service);
      return;
    }
    for (const QString& name : types) {
      if (const auto type = named_type(name); type.has_value())
        key_bindings_->BlockType(*type, true);
    }
    return;
  }

  ukui_shortcut_whitelists_[service] = types;
  if (types.isEmpty()) return;
  for (std::size_t index = 0;
       index < static_cast<std::size_t>(input::KeyBindingType::kCount);
       ++index) {
    const auto type = static_cast<input::KeyBindingType>(index);
    const char* type_name = input::KeyBindingManager::TypeName(type);
    if (type_name != nullptr &&
        !types.contains(QString::fromLatin1(type_name))) {
      key_bindings_->BlockType(type, true);
    }
  }
}

void WlcomDbusManager::RemoveUkuiShortcutService(const QString& name) {
  if (!ukui_shortcut_services_.remove(name) || key_bindings_ == nullptr) return;
  auto unblock_named = [this](const QString& type_name) {
    for (std::size_t index = 0;
         index < static_cast<std::size_t>(input::KeyBindingType::kCount);
         ++index) {
      const auto type = static_cast<input::KeyBindingType>(index);
      const char* current = input::KeyBindingManager::TypeName(type);
      if (current != nullptr && type_name == QString::fromLatin1(current)) {
        key_bindings_->BlockType(type, false);
        return;
      }
    }
  };
  if (ukui_shortcut_block_all_.remove(name)) key_bindings_->BlockAll(false);
  for (const QString& type : ukui_shortcut_blacklists_.take(name))
    unblock_named(type);
  const QStringList whitelist = ukui_shortcut_whitelists_.take(name);
  if (!whitelist.isEmpty()) {
    for (std::size_t index = 0;
         index < static_cast<std::size_t>(input::KeyBindingType::kCount);
         ++index) {
      const auto type = static_cast<input::KeyBindingType>(index);
      const char* current = input::KeyBindingManager::TypeName(type);
      if (current != nullptr &&
          !whitelist.contains(QString::fromLatin1(current))) {
        key_bindings_->BlockType(type, false);
      }
    }
  }
}

bool WlcomDbusManager::HandleGestureAction(
    const char* raw_type, const char* raw_device, const char* raw_direction,
    uint32_t fingers, const char* raw_edge, const char* raw_stage,
    const char* raw_follow_direction, double dx, double dy) {
  QJsonObject effective = system_config_.value(QStringLiteral("InputAction"))
                              .toObject()
                              .value(QStringLiteral("gesture"))
                              .toObject();
  const QJsonObject user = config_.value(QStringLiteral("InputAction"))
                               .toObject()
                               .value(QStringLiteral("gesture"))
                               .toObject();
  for (auto it = user.begin(); it != user.end(); ++it)
    effective[it.key()] = it.value();

  const QString type = QString::fromLatin1(raw_type);
  const QString device = QString::fromLatin1(raw_device);
  const QString direction = QString::fromLatin1(raw_direction);
  const QString edge = QString::fromLatin1(raw_edge);
  const QString stage = QString::fromLatin1(raw_stage);
  const QString follow_direction = QString::fromLatin1(raw_follow_direction);
  for (auto it = effective.begin(); it != effective.end(); ++it) {
    const QJsonObject action = it.value().toObject();
    if (!action.value(QStringLiteral("enable")).toBool()) continue;
    const QStringList binding = it.key().split(QLatin1Char(':'));
    if (binding.size() < 4 || binding.size() > 8 || binding[0] != type ||
        (binding[1] != device && binding[1] != QStringLiteral("any")) ||
        binding[2].toUInt() != fingers) {
      continue;
    }
    const QString binding_edge =
        binding.size() >= 5 ? binding[4] : QStringLiteral("none");
    // GXWM rejects an edge-qualified touchpad binding and treats `none` as an
    // unrestricted edge for touchscreen bindings.
    if ((device == QStringLiteral("touchpad") &&
         binding_edge != QStringLiteral("none")) ||
        (binding_edge != QStringLiteral("none") && binding_edge != edge)) {
      continue;
    }
    const QString binding_stage =
        binding.size() >= 6 ? binding[5] : QStringLiteral("trigger");
    if (binding_stage != stage) continue;

    const QStringList directions = binding[3].split(QLatin1Char('+'));
    if (!directions.contains(QStringLiteral("none")) &&
        !directions.contains(direction)) {
      continue;
    }
    if (stage == QStringLiteral("after")) {
      const double threshold =
          binding.size() >= 7 ? binding[6].toDouble() : 0.0;
      const QString required_follow =
          binding.size() >= 8 ? binding[7] : QStringLiteral("none");
      if (required_follow != follow_direction ||
          (std::abs(dx) < threshold && std::abs(dy) < threshold)) {
        continue;
      }
    }
    ExecuteInputAction(action);
    return true;
  }
  return false;
}

bool WlcomDbusManager::HandleInputAction(const QDBusMessage& message) {
  const QVariantList args = message.arguments();
  QJsonObject input_actions =
      config_.value(QStringLiteral("InputAction")).toObject();
  QJsonObject actions =
      input_actions.value(QStringLiteral("keyboard")).toObject();
  QJsonObject gestures =
      input_actions.value(QStringLiteral("gesture")).toObject();
  const QJsonObject system_actions =
      system_config_.value(QStringLiteral("InputAction"))
          .toObject()
          .value(QStringLiteral("keyboard"))
          .toObject();
  const QJsonObject system_gestures =
      system_config_.value(QStringLiteral("InputAction"))
          .toObject()
          .value(QStringLiteral("gesture"))
          .toObject();
  auto persist_actions = [&]() {
    input_actions[QStringLiteral("keyboard")] = actions;
    input_actions[QStringLiteral("gesture")] = gestures;
    config_[QStringLiteral("InputAction")] = input_actions;
    SaveConfig();
  };
  if (message.member() == QStringLiteral("ListAllActions")) {
    QList<types::StringPair> result;
    QJsonObject effective = system_actions;
    for (auto it = actions.begin(); it != actions.end(); ++it)
      effective[it.key()] = it.value();
    QJsonObject effective_gestures = system_gestures;
    for (auto it = gestures.begin(); it != gestures.end(); ++it)
      effective_gestures[it.key()] = it.value();
    for (auto it = effective.begin(); it != effective.end(); ++it)
      result << types::StringPair{
          it.key(), QString::fromUtf8(QJsonDocument(it.value().toObject())
                                          .toJson(QJsonDocument::Compact))};
    for (auto it = effective_gestures.begin(); it != effective_gestures.end();
         ++it)
      result << types::StringPair{
          it.key(), QString::fromUtf8(QJsonDocument(it.value().toObject())
                                          .toJson(QJsonDocument::Compact))};
    Reply(message, {DbusArray(result)});
    return true;
  }
  if (message.member() == QStringLiteral("ListKeyBindings")) {
    QList<types::Binding> result;
    if (key_bindings_ != nullptr) {
      for (const auto& binding : key_bindings_->Bindings()) {
        const char* type = input::KeyBindingManager::TypeName(binding.type);
        if (type != nullptr)
          result << types::Binding{QString::fromStdString(binding.shortcut),
                                   QString::fromStdString(binding.description),
                                   QString::fromLatin1(type)};
      }
    }
    Reply(message, {DbusArray(result)});
    return true;
  }
  if (message.member() == QStringLiteral("GrabNextKey")) {
    if (key_bindings_ == nullptr) {
      Error(message, kFailed, "Key binding manager unavailable.");
      return true;
    }
    key_bindings_->GrabNextKey(
        [this](bool pressed, const std::string& shortcut) {
          EmitSignal(QStringLiteral("/com/kylin/Wlcom/InputAction"),
                     QStringLiteral("com.kylin.Wlcom.InputAction"),
                     QStringLiteral("KeyEvent"),
                     {pressed, QString::fromStdString(shortcut)});
        });
    Reply(message);
    return true;
  }
  if (message.member() == QStringLiteral("CancelGrab")) {
    if (key_bindings_ != nullptr) key_bindings_->CancelGrab();
    Reply(message);
    return true;
  }
  if (message.member() == QStringLiteral("AddAction")) {
    if (args.size() != 5 ||
        (args[0].toString() != QStringLiteral("keyboard") &&
         args[0].toString() != QStringLiteral("gesture")) ||
        key_bindings_ == nullptr) {
      Error(message, kInvalidArgs, "Invalid input_type or binding.");
      return true;
    }
    const QString shortcut = args[1].toString();
    const QString description = args[2].toString();
    const QString action_data = args[3].toString();
    const QStringList action = action_data.split(',');
    const bool valid =
        (action.value(0) == QStringLiteral("command") && action.size() == 2) ||
        (action.value(0) == QStringLiteral("dbus") && action.size() == 6) ||
        (action.value(0) == QStringLiteral("button") && action.size() == 2) ||
        (action.value(0) == QStringLiteral("key") && action.size() == 3);
    if (!valid) {
      Error(message, kInvalidArgs, "Invalid action_data.");
      return true;
    }
    QJsonObject item{{QStringLiteral("enable"), true},
                     {QStringLiteral("actiontype"), action[0]},
                     {QStringLiteral("desc"), description}};
    if (args[0].toString() == QStringLiteral("keyboard"))
      item[QStringLiteral("type")] = args[4].toString();
    if (action[0] == QStringLiteral("command")) {
      item[QStringLiteral("command")] = action[1];
    } else if (action[0] == QStringLiteral("dbus")) {
      item[QStringLiteral("bustype")] = action[1];
      item[QStringLiteral("service")] = action[2];
      item[QStringLiteral("path")] = action[3];
      item[QStringLiteral("interface")] = action[4];
      item[QStringLiteral("method")] = action[5];
    } else if (action[0] == QStringLiteral("button")) {
      item[QStringLiteral("button")] = action[1];
    } else {
      item[QStringLiteral("modifiers")] = action[1];
      item[QStringLiteral("keys")] = action[2];
    }
    if (args[0].toString() == QStringLiteral("gesture")) {
      gestures[shortcut] = item;
      persist_actions();
      Reply(message);
      return true;
    }
    if (custom_binding_ids_.contains(shortcut))
      key_bindings_->Unregister(custom_binding_ids_.take(shortcut));
    const auto id = key_bindings_->Register(
        shortcut.toStdString(), input::KeyBindingType::kCustom,
        [this, item]() { ExecuteInputAction(item); },
        description.toStdString());
    if (!id.has_value()) {
      Error(message, kInvalidArgs, "Failed register input_bindings.");
      return true;
    }
    custom_binding_ids_[shortcut] = *id;
    actions[shortcut] = item;
    persist_actions();
    Reply(message);
    return true;
  }
  if (message.member() == QStringLiteral("ControlAction")) {
    if (args.size() != 2 || key_bindings_ == nullptr) {
      Error(message, kInvalidArgs, "Invalid control request.");
      return true;
    }
    const QString control = args[0].toString(), shortcut = args[1].toString();
    const bool is_gesture =
        !actions.contains(shortcut) && !system_actions.contains(shortcut) &&
        (gestures.contains(shortcut) || system_gestures.contains(shortcut));
    QJsonObject effective =
        is_gesture ? (gestures.contains(shortcut)
                          ? gestures.value(shortcut).toObject()
                          : system_gestures.value(shortcut).toObject())
                   : (actions.contains(shortcut)
                          ? actions.value(shortcut).toObject()
                          : system_actions.value(shortcut).toObject());
    if (effective.isEmpty() || (control != QStringLiteral("delete") &&
                                control != QStringLiteral("disable") &&
                                control != QStringLiteral("enable"))) {
      Error(message, kInvalidArgs, "Invalid input_bindings.");
      return true;
    }
    if (!is_gesture && custom_binding_ids_.contains(shortcut))
      key_bindings_->Unregister(custom_binding_ids_.take(shortcut));
    if (control == QStringLiteral("delete")) {
      const bool system_entry = is_gesture ? system_gestures.contains(shortcut)
                                           : system_actions.contains(shortcut);
      if (system_entry) {
        effective[QStringLiteral("enable")] = false;
        (is_gesture ? gestures : actions)[shortcut] = effective;
      } else {
        (is_gesture ? gestures : actions).remove(shortcut);
      }
    } else {
      effective[QStringLiteral("enable")] = control == QStringLiteral("enable");
      (is_gesture ? gestures : actions)[shortcut] = effective;
      if (!is_gesture && control == QStringLiteral("enable")) {
        const auto id = key_bindings_->Register(
            shortcut.toStdString(), input::KeyBindingType::kCustom,
            [this, effective]() { ExecuteInputAction(effective); },
            effective.value(QStringLiteral("desc")).toString().toStdString());
        if (id.has_value()) custom_binding_ids_[shortcut] = *id;
      }
    }
    persist_actions();
    Reply(message);
    return true;
  }
  return false;
}

}  // namespace dbus
}  // namespace flakewm
