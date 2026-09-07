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

#include <QDBusArgument>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QMetaType>
#include <QString>

#include "src/input/shortcut_settings_service.h"

namespace flakewm {
namespace input {

struct ShortcutBindingRecord {
  QString shortcut;
  QString description;
  QString type;
};

QDBusArgument& operator<<(QDBusArgument& argument,
                          const ShortcutBindingRecord& binding) {
  argument.beginStructure();
  argument << binding.shortcut << binding.description << binding.type;
  argument.endStructure();
  return argument;
}

}  // namespace input
}  // namespace flakewm

Q_DECLARE_METATYPE(flakewm::input::ShortcutBindingRecord)

namespace flakewm {
namespace input {
namespace {

constexpr char kServiceName[] = "com.kylin.Wlcom";
constexpr char kObjectPath[] = "/com/kylin/Wlcom/InputAction";
constexpr char kInterfaceName[] = "com.kylin.Wlcom.InputAction";

}  // namespace

ShortcutSettingsService::ShortcutSettingsService(
    KeyBindingManager* key_binding_manager)
    : key_binding_manager_(key_binding_manager),
      connection_(QDBusConnection::sessionBus()) {
  QDBusMetaType::registerCustomType(
      QMetaType::fromType<ShortcutBindingRecord>(), QByteArrayLiteral("(sss)"));
}

ShortcutSettingsService::~ShortcutSettingsService() {
  if (key_binding_manager_ != nullptr) {
    key_binding_manager_->CancelGrab();
  }
  if (registered_object_) {
    connection_.unregisterObject(QString::fromLatin1(kObjectPath));
  }
  if (owns_service_) {
    connection_.unregisterService(QString::fromLatin1(kServiceName));
  }
}

bool ShortcutSettingsService::Start() {
  if (key_binding_manager_ == nullptr || !connection_.isConnected()) {
    return false;
  }
  if (!connection_.registerService(QString::fromLatin1(kServiceName))) {
    ABSL_LOG(WARNING) << "Failed to obtain shortcut settings D-Bus service: "
                      << connection_.lastError().message().toStdString() << '.';
    return false;
  }
  owns_service_ = true;
  if (!connection_.registerVirtualObject(QString::fromLatin1(kObjectPath), this,
                                         QDBusConnection::SingleNode)) {
    ABSL_LOG(WARNING) << "Failed to reg shortcut settings D-Bus object: "
                      << connection_.lastError().message().toStdString();
    connection_.unregisterService(QString::fromLatin1(kServiceName));
    owns_service_ = false;
    return false;
  }
  registered_object_ = true;
  return true;
}

QString ShortcutSettingsService::introspect(const QString& path) const {
  if (path != QString::fromLatin1(kObjectPath)) {
    return {};
  }
  return QStringLiteral(
      "<interface name=\"com.kylin.Wlcom.InputAction\">"
      "<method name=\"ListKeyBindings\">"
      "<arg name=\"bindings\" type=\"a(sss)\" direction=\"out\"/>"
      "</method>"
      "<method name=\"GrabNextKey\"/>"
      "<method name=\"CancelGrab\"/>"
      "<signal name=\"KeyEvent\">"
      "<arg name=\"pressed\" type=\"b\"/>"
      "<arg name=\"shortcut\" type=\"s\"/>"
      "</signal>"
      "</interface>");
}

bool ShortcutSettingsService::handleMessage(const QDBusMessage& message,
                                            const QDBusConnection& connection) {
  if (message.path() != QString::fromLatin1(kObjectPath) ||
      message.interface() != QString::fromLatin1(kInterfaceName)) {
    return false;
  }

  if (message.member() == QStringLiteral("ListKeyBindings")) {
    QDBusArgument records;
    records.beginArray(QMetaType::fromType<ShortcutBindingRecord>());
    for (const KeyBindingManager::BindingInfo& binding :
         key_binding_manager_->Bindings()) {
      const char* type = KeyBindingManager::TypeName(binding.type);
      if (type == nullptr) {
        continue;
      }
      records << ShortcutBindingRecord{
          .shortcut = QString::fromStdString(binding.shortcut),
          .description = QString::fromStdString(binding.description),
          .type = QString::fromLatin1(type),
      };
    }
    records.endArray();
    connection.send(message.createReply(QVariant::fromValue(records)));
    return true;
  }

  if (message.member() == QStringLiteral("GrabNextKey")) {
    key_binding_manager_->GrabNextKey(
        [this](bool pressed, const std::string& shortcut) {
          EmitKeyEvent(pressed, shortcut);
        });
    connection.send(message.createReply());
    return true;
  }

  if (message.member() == QStringLiteral("CancelGrab")) {
    key_binding_manager_->CancelGrab();
    connection.send(message.createReply());
    return true;
  }

  return false;
}

void ShortcutSettingsService::EmitKeyEvent(bool pressed,
                                           const std::string& shortcut) {
  QDBusMessage message = QDBusMessage::createSignal(
      QString::fromLatin1(kObjectPath), QString::fromLatin1(kInterfaceName),
      QStringLiteral("KeyEvent"));
  message << pressed << QString::fromStdString(shortcut);
  connection_.send(message);
}

}  // namespace input
}  // namespace flakewm
