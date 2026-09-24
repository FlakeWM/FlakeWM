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

void WlcomDbusManager::NotifySelectionChanged(bool primary,
                                              wlr_surface* surface) {
  const int pid = SurfacePid(surface);
  int& current = primary ? primary_selection_pid_ : clipboard_pid_;
  if (current == pid) return;
  current = pid;
  EmitSignal(QStringLiteral("/Clipboard"),
             QStringLiteral("org.kde.KWin.Clipboard"),
             primary ? QStringLiteral("primarySelectionPidChanged")
                     : QStringLiteral("clipboardSelectionPidChanged"),
             {pid});
}

bool WlcomDbusManager::HandleClipboard(const QDBusMessage& message) {
  if (message.member() == QStringLiteral("getClipboardSelectionPid")) {
    Reply(message, {clipboard_pid_});
    return true;
  }
  if (message.member() == QStringLiteral("getPrimarySelectionPid")) {
    Reply(message, {primary_selection_pid_});
    return true;
  }
  return false;
}

void WlcomDbusManager::NotifyToplevelActivated(const char* app_id,
                                               const char* title) {
  const QString id = QString::fromUtf8(app_id == nullptr ? "Unknown" : app_id);
  if (id == QStringLiteral("HuskyPanel")) return;
  QDBusMessage call = QDBusMessage::createMethodCall(
      QStringLiteral("marcus.panel.util.AppBridge"),
      QStringLiteral("/AppBridge"),
      QStringLiteral("marcus.panel.util.AppBridge"),
      QStringLiteral("UpdateActiveApp"));
  call << id << id << QString::fromUtf8(title == nullptr ? "Unknown" : title);
  session_bus_.send(call);
}

QString WlcomDbusManager::ComponentPath(const QString& name) {
  QString escaped = name;
  for (QChar& character : escaped)
    if (!character.isLetterOrNumber() || character.unicode() > 0x7f)
      character = QLatin1Char('_');
  return QStringLiteral("/component/") + escaped;
}

WlcomDbusManager::GlobalShortcutComponent* WlcomDbusManager::FindComponent(
    const QString& name) {
  for (auto& component : components_)
    if (component->unique_name == name || component->path == name)
      return component.get();
  return nullptr;
}

const WlcomDbusManager::GlobalShortcutComponent*
WlcomDbusManager::FindComponent(const QString& name) const {
  for (const auto& component : components_)
    if (component->unique_name == name || component->path == name)
      return component.get();
  return nullptr;
}

void WlcomDbusManager::ActivateGlobalShortcut(
    GlobalShortcutComponent* component, GlobalShortcut* shortcut) {
  if (component == nullptr || shortcut == nullptr || key_bindings_ == nullptr ||
      shortcut->binding_id.has_value() || !shortcut->present ||
      shortcut->key == 0) {
    return;
  }
  const QString portable =
      QKeySequence(shortcut->key).toString(QKeySequence::PortableText);
  if (portable.isEmpty()) return;
  const QString component_name = component->unique_name;
  const QString action_name = shortcut->unique_name;
  shortcut->binding_id = key_bindings_->Register(
      (portable + QStringLiteral(":no")).toStdString(),
      input::KeyBindingType::kCustom,
      [this, component_name, action_name]() {
        GlobalShortcutComponent* current = FindComponent(component_name);
        if (current == nullptr) return;
        const auto item =
            std::find_if(current->shortcuts.begin(), current->shortcuts.end(),
                         [&](const GlobalShortcut& candidate) {
                           return candidate.unique_name == action_name;
                         });
        if (item == current->shortcuts.end() || !item->present) return;
        EmitSignal(current->path,
                   QStringLiteral("org.kde.kglobalaccel.Component"),
                   QStringLiteral("globalShortcutPressed"),
                   {component_name, action_name, static_cast<qlonglong>(0)});
      },
      shortcut->friendly_name.toStdString());
}

void WlcomDbusManager::DeactivateGlobalShortcut(GlobalShortcut* shortcut) {
  if (shortcut == nullptr || key_bindings_ == nullptr ||
      !shortcut->binding_id.has_value()) {
    return;
  }
  key_bindings_->Unregister(*shortcut->binding_id);
  shortcut->binding_id.reset();
}

bool WlcomDbusManager::HandleKGlobalAccel(const QDBusMessage& message) {
  const QVariantList args = message.arguments();
  auto action_id = [&]() { return ReadStringList(args.value(0)); };
  auto shortcut_for = [&](const QStringList& id) -> GlobalShortcut* {
    if (id.size() < 2) return nullptr;
    GlobalShortcutComponent* component = FindComponent(id[0]);
    if (component == nullptr) return nullptr;
    auto it =
        std::find_if(component->shortcuts.begin(), component->shortcuts.end(),
                     [&](const GlobalShortcut& shortcut) {
                       return shortcut.unique_name == id[1];
                     });
    return it == component->shortcuts.end() ? nullptr : &*it;
  };
  if (message.member() == QStringLiteral("actionList")) {
    const types::KeySequence key = ReadKeySequence(args.value(0));
    QStringList result;
    if (!key.keys.isEmpty()) {
      for (const auto& component : components_)
        for (const auto& shortcut : component->shortcuts)
          if (shortcut.key == key.keys[0])
            result = {component->unique_name, shortcut.unique_name,
                      component->friendly_name, shortcut.friendly_name};
    }
    Reply(message, {result});
    return true;
  }
  if (message.member() == QStringLiteral("activateGlobalShortcutContext")) {
    GlobalShortcutComponent* component =
        FindComponent(args.value(0).toString());
    if (component != nullptr) component->context = args.value(1).toString();
    Reply(message);
    return true;
  }
  if (message.member() == QStringLiteral("allActionsForComponent")) {
    const QStringList id = action_id();
    QList<QStringList> result;
    if (!id.isEmpty())
      if (const auto* component = FindComponent(id[0]))
        for (const auto& shortcut : component->shortcuts)
          if (!shortcut.fresh)
            result << QStringList{component->unique_name, shortcut.unique_name,
                                  component->friendly_name,
                                  shortcut.friendly_name};
    Reply(message, {QVariant::fromValue(result)});
    return true;
  }
  if (message.member() == QStringLiteral("allComponents")) {
    QList<QDBusObjectPath> paths;
    for (const auto& component : components_)
      paths << QDBusObjectPath(component->path);
    Reply(message, {QVariant::fromValue(paths)});
    return true;
  }
  if (message.member() == QStringLiteral("allMainComponents")) {
    QList<QStringList> result;
    for (const auto& component : components_)
      result << QStringList{component->unique_name, QString{},
                            component->friendly_name, QString{}};
    Reply(message, {QVariant::fromValue(result)});
    return true;
  }
  if (message.member() == QStringLiteral("blockGlobalShortcuts")) {
    const bool block = args.value(0).toBool();
    if (key_bindings_ != nullptr && block != shortcuts_blocked_)
      key_bindings_->BlockAll(block);
    shortcuts_blocked_ = block;
    Reply(message);
    return true;
  }
  if (message.member() == QStringLiteral("defaultShortcutKeys") ||
      message.member() == QStringLiteral("shortcutKeys")) {
    GlobalShortcut* shortcut = shortcut_for(action_id());
    QList<types::KeySequence> result;
    if (shortcut != nullptr)
      result << types::KeySequence{{message.member().startsWith("default")
                                        ? shortcut->default_key
                                        : shortcut->key,
                                    0, 0, 0}};
    Reply(message, {DbusArray(result)});
    return true;
  }
  if (message.member() == QStringLiteral("doRegister")) {
    const QStringList id = action_id();
    if (id.size() != 4) {
      Error(message, kInvalidArgs, "Action id needs four strings.");
      return true;
    }
    GlobalShortcutComponent* component = FindComponent(id[0]);
    if (component == nullptr) {
      auto created = std::make_unique<GlobalShortcutComponent>();
      created->unique_name = id[0];
      created->friendly_name = id[2];
      created->path = ComponentPath(id[0]);
      component = created.get();
      components_.push_back(std::move(created));
    }
    auto existing = std::find_if(
        component->shortcuts.begin(), component->shortcuts.end(),
        [&](const auto& item) { return item.unique_name == id[1]; });
    if (existing == component->shortcuts.end())
      component->shortcuts.push_back({id[1], id[3]});
    else {
      if (!id[2].isEmpty()) component->friendly_name = id[2];
      if (!id[3].isEmpty()) existing->friendly_name = id[3];
    }
    Reply(message);
    return true;
  }
  if (message.member() == QStringLiteral("getComponent")) {
    const auto* component = FindComponent(args.value(0).toString());
    if (component == nullptr)
      Error(message, QStringLiteral("org.kde.kglobalaccel.NoSuchComponent"),
            QStringLiteral("The component doesn't exist."));
    else
      Reply(message, {QVariant::fromValue(QDBusObjectPath(component->path))});
    return true;
  }
  if (message.member() == QStringLiteral("globalShortcutAvailable")) {
    const types::KeySequence key = ReadKeySequence(args.value(0));
    const auto* component = FindComponent(args.value(1).toString());
    bool found = false;
    if (component != nullptr && !key.keys.isEmpty())
      found = std::any_of(
          component->shortcuts.begin(), component->shortcuts.end(),
          [&](const auto& item) { return item.key == key.keys[0]; });
    Reply(message, {found});
    return true;
  }
  if (message.member() == QStringLiteral("globalShortcutsByKey")) {
    const types::KeySequence key = ReadKeySequence(args.value(0));
    QList<types::ShortcutInfo> result;
    if (!key.keys.isEmpty())
      for (const auto& component : components_)
        for (const auto& shortcut : component->shortcuts)
          if (shortcut.key == key.keys[0])
            result << types::ShortcutInfo{
                component->context,      component->context_friendly,
                component->unique_name,  component->friendly_name,
                shortcut.unique_name,    shortcut.friendly_name,
                {shortcut.key, 0, 0, 0}, {shortcut.default_key, 0, 0, 0}};
    Reply(message, {DbusArray(result)});
    return true;
  }
  if (message.member() == QStringLiteral("setForeignShortcutKeys") ||
      message.member() == QStringLiteral("setShortcutKeys")) {
    const QStringList id = action_id();
    GlobalShortcut* shortcut = shortcut_for(id);
    const QList<types::KeySequence> keys = ReadKeySequences(args.value(1));
    const qint32 requested_key =
        !keys.isEmpty() && !keys[0].keys.isEmpty() ? keys[0].keys[0] : 0;
    QList<types::KeySequence> result;
    if (shortcut != nullptr) {
      GlobalShortcutComponent* component =
          id.isEmpty() ? nullptr : FindComponent(id[0]);
      if (message.member() == QStringLiteral("setForeignShortcutKeys")) {
        DeactivateGlobalShortcut(shortcut);
        shortcut->key = requested_key;
        ActivateGlobalShortcut(component, shortcut);
        const QStringList changed{
            id.value(0), id.value(1),
            component == nullptr ? QString{} : component->friendly_name,
            shortcut->friendly_name};
        EmitSignal(
            QStringLiteral("/kglobalaccel"),
            QStringLiteral("org.kde.KGlobalAccel"),
            QStringLiteral("yourShortcutsChanged"),
            {changed, DbusArray(QList<types::KeySequence>{
                          types::KeySequence{{shortcut->key, 0, 0, 0}}})});
      } else {
        const quint32 flags = args.value(2).toUInt();
        constexpr quint32 kSetPresent = 2;
        constexpr quint32 kNoAutoloading = 4;
        constexpr quint32 kIsDefault = 8;
        if ((flags & kIsDefault) != 0) {
          shortcut->default_key = requested_key;
          result << types::KeySequence{{requested_key, 0, 0, 0}};
        } else if ((flags & kNoAutoloading) == 0 && !shortcut->fresh) {
          if (!shortcut->present && (flags & kSetPresent) != 0) {
            shortcut->present = true;
            ActivateGlobalShortcut(component, shortcut);
          }
          result << types::KeySequence{{shortcut->key, 0, 0, 0}};
        } else {
          DeactivateGlobalShortcut(shortcut);
          shortcut->key = requested_key;
          shortcut->fresh = false;
          shortcut->present = (flags & kSetPresent) != 0;
          ActivateGlobalShortcut(component, shortcut);
          result << types::KeySequence{{shortcut->key, 0, 0, 0}};
        }
      }
    }
    if (message.member() == QStringLiteral("setShortcutKeys"))
      Reply(message, {DbusArray(result)});
    else
      Reply(message);
    return true;
  }
  if (message.member() == QStringLiteral("setInactive")) {
    if (GlobalShortcut* shortcut = shortcut_for(action_id())) {
      shortcut->present = false;
      DeactivateGlobalShortcut(shortcut);
    }
    Reply(message);
    return true;
  }
  if (message.member() == QStringLiteral("unregister")) {
    GlobalShortcutComponent* component =
        FindComponent(args.value(0).toString());
    bool removed = false;
    if (component != nullptr) {
      const auto size = component->shortcuts.size();
      for (auto& shortcut : component->shortcuts)
        if (shortcut.unique_name == args.value(1).toString())
          DeactivateGlobalShortcut(&shortcut);
      std::erase_if(component->shortcuts, [&](const auto& item) {
        return item.unique_name == args.value(1).toString();
      });
      removed = component->shortcuts.size() != size;
    }
    Reply(message, {removed});
    return true;
  }
  return false;
}

bool WlcomDbusManager::HandleKGlobalAccelComponent(
    const QDBusMessage& message) {
  GlobalShortcutComponent* component = FindComponent(message.path());
  if (component == nullptr) return false;
  if (message.member() == QStringLiteral("allShortcutInfos")) {
    QList<types::ShortcutInfo> result;
    for (const auto& shortcut : component->shortcuts)
      result << types::ShortcutInfo{
          component->context,      component->context_friendly,
          component->unique_name,  component->friendly_name,
          shortcut.unique_name,    shortcut.friendly_name,
          {shortcut.key, 0, 0, 0}, {shortcut.default_key, 0, 0, 0}};
    Reply(message, {DbusArray(result)});
    return true;
  }
  if (message.member() == QStringLiteral("cleanUp")) {
    const auto size = component->shortcuts.size();
    for (auto& shortcut : component->shortcuts)
      if (!shortcut.present) DeactivateGlobalShortcut(&shortcut);
    std::erase_if(component->shortcuts,
                  [](const auto& item) { return !item.present; });
    Reply(message, {component->shortcuts.size() != size});
    return true;
  }
  if (message.member() == QStringLiteral("getShortcutContexts")) {
    Reply(message, {QStringList{component->context}});
    return true;
  }
  if (message.member() == QStringLiteral("invokeShortcut")) {
    const QString action = message.arguments().value(0).toString();
    auto it = std::find_if(
        component->shortcuts.begin(), component->shortcuts.end(),
        [&](const auto& item) { return item.unique_name == action; });
    if (it != component->shortcuts.end())
      EmitSignal(
          component->path, QStringLiteral("org.kde.kglobalaccel.Component"),
          QStringLiteral("globalShortcutPressed"),
          {component->unique_name, it->unique_name, static_cast<qlonglong>(0)});
    Reply(message);
    return true;
  }
  if (message.member() == QStringLiteral("isActive")) {
    Reply(message,
          {std::any_of(component->shortcuts.begin(), component->shortcuts.end(),
                       [](const auto& item) { return item.present; })});
    return true;
  }
  if (message.member() == QStringLiteral("shortcutNames")) {
    QStringList result;
    for (const auto& shortcut : component->shortcuts)
      result << shortcut.unique_name;
    Reply(message, {result});
    return true;
  }
  return false;
}

bool WlcomDbusManager::HandleKdeInput(const QDBusMessage&) { return false; }

bool WlcomDbusManager::HandleProperties(const QDBusMessage& message) {
  const QVariantList args = message.arguments();
  if (args.isEmpty()) {
    Error(message, kInvalidArgs, "Missing interface.");
    return true;
  }
  const QString interface = args[0].toString();
  auto device_for_path = [&]() -> InputDevice* {
    const QString prefix = QStringLiteral("/org/kde/KWin/InputDevice/");
    if (!message.path().startsWith(prefix)) return nullptr;
    return FindKdeInput(message.path().mid(prefix.size()));
  };
  auto value_for = [&](const QString& property, bool* found) -> QVariant {
    *found = true;
    if (interface == QStringLiteral("org.kde.KWin.Clipboard")) {
      if (property == QStringLiteral("GetClipboardSelectionPid"))
        return clipboard_pid_;
      if (property == QStringLiteral("GetPrimarySelectionPid"))
        return primary_selection_pid_;
    }
    if (interface == QStringLiteral("org.kde.KWin.InputDeviceManager") &&
        property == QStringLiteral("devicesSysNames")) {
      QStringList names;
      for (const auto& input : inputs_)
        if (input->device != nullptr && input->libinput)
          names << input->sys_name;
      return names;
    }
    if (interface == QStringLiteral("org.kde.kglobalaccel.Component")) {
      const auto* component = FindComponent(message.path());
      if (component != nullptr && property == QStringLiteral("friendlyName"))
        return component->friendly_name;
      if (component != nullptr && property == QStringLiteral("uniqueName"))
        return component->unique_name;
    }
    if (interface == QStringLiteral("org.kde.KWin.InputDevice")) {
      InputDevice* input = device_for_path();
      if (input == nullptr || input->device == nullptr) {
        *found = false;
        return {};
      }
      wlr_input_device* device = input->device;
      libinput_device* libinput = Libinput(device);
      const bool pointer = device->type == WLR_INPUT_DEVICE_POINTER;
      const bool keyboard = device->type == WLR_INPUT_DEVICE_KEYBOARD;
      if (property == QStringLiteral("pointer")) return pointer;
      if (property == QStringLiteral("keyboard")) return keyboard;
      if (property == QStringLiteral("touchpad"))
        return pointer && libinput != nullptr &&
               libinput_device_config_tap_get_finger_count(libinput) > 0;
      if (property == QStringLiteral("touch"))
        return device->type == WLR_INPUT_DEVICE_TOUCH;
      if (property == QStringLiteral("tabletTool"))
        return device->type == WLR_INPUT_DEVICE_TABLET;
      if (property == QStringLiteral("tabletPad"))
        return device->type == WLR_INPUT_DEVICE_TABLET_PAD;
      if (property == QStringLiteral("switchDevice"))
        return device->type == WLR_INPUT_DEVICE_SWITCH;
      if (property == QStringLiteral("name"))
        return QString::fromUtf8(device->name);
      if (property == QStringLiteral("sysName")) return input->sys_name;
      if (property == QStringLiteral("product"))
        return libinput == nullptr
                   ? 0
                   : static_cast<qint32>(
                         libinput_device_get_id_product(libinput));
      if (property == QStringLiteral("vendor"))
        return libinput == nullptr
                   ? 0
                   : static_cast<qint32>(
                         libinput_device_get_id_vendor(libinput));
      if (libinput == nullptr) {
        *found = false;
        return {};
      }
      const quint32 send_modes =
          libinput_device_config_send_events_get_modes(libinput);
      if (property == QStringLiteral("supportsDisableEvents"))
        return (send_modes & LIBINPUT_CONFIG_SEND_EVENTS_DISABLED) != 0;
      if (property == QStringLiteral("supportsDisableEventsOnExternalMouse"))
        return (send_modes &
                LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE) != 0;
      if (property == QStringLiteral("enabled"))
        return libinput_device_config_send_events_get_mode(libinput) ==
               LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;
      if (property == QStringLiteral("supportsPointerAcceleration"))
        return libinput_device_config_accel_is_available(libinput) != 0;
      if (property == QStringLiteral("defaultPointerAcceleration"))
        return libinput_device_config_accel_get_default_speed(libinput);
      if (property == QStringLiteral("pointerAcceleration"))
        return libinput_device_config_accel_get_speed(libinput);
      if (property == QStringLiteral("supportsLeftHanded"))
        return libinput_device_config_left_handed_is_available(libinput) != 0;
      if (property == QStringLiteral("leftHandedEnabledByDefault"))
        return libinput_device_config_left_handed_get_default(libinput) != 0;
      if (property == QStringLiteral("leftHanded"))
        return libinput_device_config_left_handed_get(libinput) != 0;
      if (property ==
          QStringLiteral("supportsPointerAccelerationProfileAdaptive"))
        return (libinput_device_config_accel_get_profiles(libinput) &
                LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE) != 0;
      if (property ==
          QStringLiteral("defaultPointerAccelerationProfileAdaptive"))
        return (libinput_device_config_accel_get_default_profile(libinput) &
                LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE) != 0;
      if (property == QStringLiteral("pointerAccelerationProfileAdaptive"))
        return (libinput_device_config_accel_get_profile(libinput) &
                LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE) != 0;
      if (property == QStringLiteral("supportsNaturalScroll"))
        return libinput_device_config_scroll_has_natural_scroll(libinput) != 0;
      if (property == QStringLiteral("naturalScrollEnabledByDefault"))
        return libinput_device_config_scroll_get_default_natural_scroll_enabled(
                   libinput) != 0;
      if (property == QStringLiteral("naturalScroll"))
        return libinput_device_config_scroll_get_natural_scroll_enabled(
                   libinput) != 0;
      if (property == QStringLiteral("tapFingerCount"))
        return libinput_device_config_tap_get_finger_count(libinput) != 0;
      if (property == QStringLiteral("tapToClickEnabledByDefault"))
        return libinput_device_config_tap_get_default_enabled(libinput) ==
               LIBINPUT_CONFIG_TAP_ENABLED;
      if (property == QStringLiteral("tapToClick"))
        return libinput_device_config_tap_get_enabled(libinput) ==
               LIBINPUT_CONFIG_TAP_ENABLED;
      const quint32 scroll_methods =
          libinput_device_config_scroll_get_methods(libinput);
      const quint32 default_scroll =
          libinput_device_config_scroll_get_default_method(libinput);
      const quint32 scroll = libinput_device_config_scroll_get_method(libinput);
      if (property == QStringLiteral("supportsScrollTwoFinger"))
        return (scroll_methods & LIBINPUT_CONFIG_SCROLL_2FG) != 0;
      if (property == QStringLiteral("scrollTwoFingerEnabledByDefault"))
        return (default_scroll & LIBINPUT_CONFIG_SCROLL_2FG) != 0;
      if (property == QStringLiteral("scrollTwoFinger"))
        return (scroll & LIBINPUT_CONFIG_SCROLL_2FG) != 0;
      if (property == QStringLiteral("supportsScrollEdge"))
        return (scroll_methods & LIBINPUT_CONFIG_SCROLL_EDGE) != 0;
      if (property == QStringLiteral("scrollEdgeEnabledByDefault"))
        return (default_scroll & LIBINPUT_CONFIG_SCROLL_EDGE) != 0;
      if (property == QStringLiteral("scrollEdge"))
        return (scroll & LIBINPUT_CONFIG_SCROLL_EDGE) != 0;
      if (property == QStringLiteral("supportsDisableWhileTyping"))
        return libinput_device_config_dwt_is_available(libinput) != 0;
      if (property == QStringLiteral("disableWhileTypingEnabledByDefault"))
        return libinput_device_config_dwt_get_default_enabled(libinput) ==
               LIBINPUT_CONFIG_DWT_ENABLED;
      if (property == QStringLiteral("disableWhileTyping"))
        return libinput_device_config_dwt_get_enabled(libinput) ==
               LIBINPUT_CONFIG_DWT_ENABLED;
      if (property == QStringLiteral("outputName")) {
        return config_.value(QStringLiteral("Inputs"))
            .toObject()
            .value(input->name)
            .toObject()
            .value(QStringLiteral("mapped_to_output"))
            .toString();
      }
    }
    *found = false;
    return {};
  };

  if (message.member() == QStringLiteral("Get")) {
    if (args.size() != 2) {
      Error(message, kInvalidArgs, "Get expects interface and property.");
      return true;
    }
    bool found = false;
    const QVariant value = value_for(args[1].toString(), &found);
    if (!found)
      Error(message,
            QStringLiteral("org.freedesktop.DBus.Error.UnknownProperty"),
            QStringLiteral("Unknown property."));
    else
      Reply(message, {QVariant::fromValue(QDBusVariant(value))});
    return true;
  }
  if (message.member() == QStringLiteral("GetAll")) {
    QStringList properties;
    if (interface == QStringLiteral("org.kde.KWin.Clipboard"))
      properties = {QStringLiteral("GetClipboardSelectionPid"),
                    QStringLiteral("GetPrimarySelectionPid")};
    else if (interface == QStringLiteral("org.kde.KWin.InputDeviceManager"))
      properties = {QStringLiteral("devicesSysNames")};
    else if (interface == QStringLiteral("org.kde.kglobalaccel.Component"))
      properties = {QStringLiteral("friendlyName"),
                    QStringLiteral("uniqueName")};
    else if (interface == QStringLiteral("org.kde.KWin.InputDevice"))
      properties = {"pointer",
                    "keyboard",
                    "touchpad",
                    "touch",
                    "tabletTool",
                    "tabletPad",
                    "switchDevice",
                    "name",
                    "sysName",
                    "product",
                    "vendor",
                    "supportsDisableEvents",
                    "supportsDisableEventsOnExternalMouse",
                    "enabled",
                    "supportsPointerAcceleration",
                    "defaultPointerAcceleration",
                    "pointerAcceleration",
                    "supportsLeftHanded",
                    "leftHandedEnabledByDefault",
                    "leftHanded",
                    "supportsPointerAccelerationProfileAdaptive",
                    "defaultPointerAccelerationProfileAdaptive",
                    "pointerAccelerationProfileAdaptive",
                    "supportsNaturalScroll",
                    "naturalScrollEnabledByDefault",
                    "naturalScroll",
                    "tapFingerCount",
                    "tapToClickEnabledByDefault",
                    "tapToClick",
                    "supportsScrollTwoFinger",
                    "scrollTwoFingerEnabledByDefault",
                    "scrollTwoFinger",
                    "supportsScrollEdge",
                    "scrollEdgeEnabledByDefault",
                    "scrollEdge",
                    "supportsDisableWhileTyping",
                    "disableWhileTypingEnabledByDefault",
                    "disableWhileTyping",
                    "outputName"};
    QVariantMap result;
    for (const QString& property : properties) {
      bool found = false;
      const QVariant value = value_for(property, &found);
      if (found) result[property] = value;
    }
    Reply(message, {result});
    return true;
  }
  if (message.member() == QStringLiteral("Set")) {
    if (args.size() != 3 ||
        interface != QStringLiteral("org.kde.KWin.InputDevice")) {
      Error(message,
            QStringLiteral("org.freedesktop.DBus.Error.PropertyReadOnly"),
            QStringLiteral("Property is read-only."));
      return true;
    }
    InputDevice* input = device_for_path();
    libinput_device* libinput =
        input == nullptr ? nullptr : Libinput(input->device);
    const QString property = args[1].toString();
    const QVariant value = UnwrapVariant(args[2]);
    bool ok = libinput != nullptr;
    if (ok && property == QStringLiteral("enabled"))
      ok = libinput_device_config_send_events_set_mode(
               libinput, value.toBool()
                             ? LIBINPUT_CONFIG_SEND_EVENTS_ENABLED
                             : LIBINPUT_CONFIG_SEND_EVENTS_DISABLED) ==
           LIBINPUT_CONFIG_STATUS_SUCCESS;
    else if (ok && property == QStringLiteral("pointerAcceleration"))
      ok = libinput_device_config_accel_set_speed(libinput, value.toDouble()) ==
           LIBINPUT_CONFIG_STATUS_SUCCESS;
    else if (ok && property == QStringLiteral("leftHanded"))
      ok = libinput_device_config_left_handed_set(libinput, value.toBool()) ==
           LIBINPUT_CONFIG_STATUS_SUCCESS;
    else if (ok &&
             property == QStringLiteral("pointerAccelerationProfileAdaptive"))
      ok = libinput_device_config_accel_set_profile(
               libinput, value.toBool() ? LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE
                                        : LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT) ==
           LIBINPUT_CONFIG_STATUS_SUCCESS;
    else if (ok && property == QStringLiteral("naturalScroll"))
      ok = libinput_device_config_scroll_set_natural_scroll_enabled(
               libinput, value.toBool()) == LIBINPUT_CONFIG_STATUS_SUCCESS;
    else if (ok && property == QStringLiteral("tapToClick"))
      ok = libinput_device_config_tap_set_enabled(
               libinput, value.toBool() ? LIBINPUT_CONFIG_TAP_ENABLED
                                        : LIBINPUT_CONFIG_TAP_DISABLED) ==
           LIBINPUT_CONFIG_STATUS_SUCCESS;
    else if (ok && (property == QStringLiteral("scrollTwoFinger") ||
                    property == QStringLiteral("scrollEdge")))
      ok = libinput_device_config_scroll_set_method(
               libinput, value.toBool()
                             ? (property == QStringLiteral("scrollTwoFinger")
                                    ? LIBINPUT_CONFIG_SCROLL_2FG
                                    : LIBINPUT_CONFIG_SCROLL_EDGE)
                             : LIBINPUT_CONFIG_SCROLL_NO_SCROLL) ==
           LIBINPUT_CONFIG_STATUS_SUCCESS;
    else if (ok && property == QStringLiteral("disableWhileTyping"))
      ok = libinput_device_config_dwt_set_enabled(
               libinput, value.toBool() ? LIBINPUT_CONFIG_DWT_ENABLED
                                        : LIBINPUT_CONFIG_DWT_DISABLED) ==
           LIBINPUT_CONFIG_STATUS_SUCCESS;
    else if (ok && property == QStringLiteral("outputName")) {
      const QString name = value.toString();
      wlr_output* output = name.isEmpty() ? nullptr : FindOutput(name);
      ok = name.isEmpty() || output != nullptr;
      if (ok) {
        wlr_cursor_map_input_to_output(compositor_->cursor_, input->device,
                                       output);
      }
    } else
      ok = false;
    if (!ok)
      Error(message, kInvalidArgs, "Invalid property value.");
    else {
      QJsonObject inputs = config_.value(QStringLiteral("Inputs")).toObject();
      QJsonObject state = inputs.value(input->name).toObject();
      if (property == QStringLiteral("enabled"))
        state[QStringLiteral("send_events_mode")] =
            value.toBool() ? LIBINPUT_CONFIG_SEND_EVENTS_ENABLED
                           : LIBINPUT_CONFIG_SEND_EVENTS_DISABLED;
      else if (property == QStringLiteral("pointerAcceleration"))
        state[QStringLiteral("pointer_accel_speed")] = value.toDouble();
      else if (property == QStringLiteral("leftHanded"))
        state[QStringLiteral("left_handed")] = value.toBool();
      else if (property == QStringLiteral("pointerAccelerationProfileAdaptive"))
        state[QStringLiteral("accel_profile")] =
            value.toBool() ? LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE
                           : LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT;
      else if (property == QStringLiteral("naturalScroll"))
        state[QStringLiteral("natural_scroll")] = value.toBool();
      else if (property == QStringLiteral("tapToClick"))
        state[QStringLiteral("tap_to_click")] = value.toBool();
      else if (property == QStringLiteral("scrollTwoFinger"))
        state[QStringLiteral("scroll_method")] =
            value.toBool() ? LIBINPUT_CONFIG_SCROLL_2FG
                           : LIBINPUT_CONFIG_SCROLL_NO_SCROLL;
      else if (property == QStringLiteral("scrollEdge"))
        state[QStringLiteral("scroll_method")] =
            value.toBool() ? LIBINPUT_CONFIG_SCROLL_EDGE
                           : LIBINPUT_CONFIG_SCROLL_NO_SCROLL;
      else if (property == QStringLiteral("disableWhileTyping"))
        state[QStringLiteral("dwt")] = value.toBool();
      else if (property == QStringLiteral("outputName"))
        state[QStringLiteral("mapped_to_output")] = value.toString();
      inputs[input->name] = state;
      config_[QStringLiteral("Inputs")] = inputs;
      SaveConfig();
      Reply(message);
    }
    return true;
  }
  return false;
}

}  // namespace dbus
}  // namespace flakewm
