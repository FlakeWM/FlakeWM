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

#ifndef SRC_INPUT_SHORTCUT_SETTINGS_SERVICE_H_
#define SRC_INPUT_SHORTCUT_SETTINGS_SERVICE_H_

#include <QDBusConnection>
#include <QDBusVirtualObject>

#include "src/input/key_binding_manager.h"

namespace flakewm {
namespace input {

class ShortcutSettingsService final : public QDBusVirtualObject {
 public:
  explicit ShortcutSettingsService(KeyBindingManager* key_binding_manager);
  ~ShortcutSettingsService() override;

  ShortcutSettingsService(const ShortcutSettingsService&) = delete;
  ShortcutSettingsService& operator=(const ShortcutSettingsService&) = delete;

  bool Start();

  QString introspect(const QString& path) const override;
  bool handleMessage(const QDBusMessage& message,
                     const QDBusConnection& connection) override;

 private:
  void EmitKeyEvent(bool pressed, const std::string& shortcut);

  KeyBindingManager* key_binding_manager_;
  QDBusConnection connection_;
  bool owns_service_ = false;
  bool registered_object_ = false;
};

}  // namespace input
}  // namespace flakewm

#endif  // SRC_INPUT_SHORTCUT_SETTINGS_SERVICE_H_
