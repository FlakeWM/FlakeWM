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

#ifndef SRC_DBUS_WLCOM_WLCOM_DBUS_MANAGER_H_
#define SRC_DBUS_WLCOM_WLCOM_DBUS_MANAGER_H_

#include <QDBusConnection>
#include <QDBusVirtualObject>
#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QStringList>
#include <memory>
#include <vector>

#ifdef FLAKEWM_HAS_QGSETTINGS
class QGSettings;
#endif

#include "src/utils/signal_listener.h"
#include "src/wlr_wrapper/wlroots.h"

namespace flakewm {
namespace core {
class CompositorPrivate;
}
namespace input {
class KeyBindingManager;
}

namespace dbus {

// Owns every session-bus name and object exported by gxde-wlcom.  One virtual
// object is used because Qt registers object paths per connection (not per
// well-known name), while Wlcom intentionally exposes aliases under several
// different service names.
class WlcomDbusManager final : public QDBusVirtualObject {
 public:
  WlcomDbusManager(core::CompositorPrivate* compositor,
                   input::KeyBindingManager* key_bindings);
  ~WlcomDbusManager() override;

  WlcomDbusManager(const WlcomDbusManager&) = delete;
  WlcomDbusManager& operator=(const WlcomDbusManager&) = delete;

  bool Start();
  void UpdateActivationEnvironment();
  void AddOutput(wlr_output* output);
  void RemoveOutput(wlr_output* output);
  void AddInput(wlr_input_device* device);
  void NotifyToplevelActivated(const char* app_id, const char* title);
  void NotifySelectionChanged(bool primary, wlr_surface* surface);
  wlr_color_transform* OutputColorTransform(wlr_output* output) const;

  QString introspect(const QString& path) const override;
  bool handleMessage(const QDBusMessage& message,
                     const QDBusConnection& connection) override;

 private:
  struct InputDevice;
  struct GlobalShortcut;
  struct GlobalShortcutComponent;
  struct Watermark;

  bool RegisterNames();
  bool RegisterObjects();
  void LoadConfig();
  void LoadInputActions();
  void SetupUkuiShortcutIntegration();
  void AddUkuiShortcutService(const QString& name);
  void RemoveUkuiShortcutService(const QString& name);
  void ApplyUkuiShortcutTypes(const QString& service, const QStringList& types,
                              bool whitelist);
  void ExecuteInputAction(const QJsonObject& action);
  bool HandleGestureAction(const char* type, const char* device,
                           const char* direction, uint32_t fingers,
                           const char* edge, const char* stage,
                           const char* follow_direction, double dx, double dy);
  void SaveConfig() const;
  void ApplyInputConfig(InputDevice* input);
  void RemoveInput(InputDevice* input);
  void EmitSignal(const QString& path, const QString& interface,
                  const QString& member, const QVariantList& arguments = {});
  void Reply(const QDBusMessage& message,
             const QVariantList& arguments = {}) const;
  void Error(const QDBusMessage& message, const QString& name,
             const QString& text) const;

  bool HandleCommon(const QDBusMessage& message);
  bool HandleOutput(const QDBusMessage& message);
  bool HandleView(const QDBusMessage& message);
  bool HandleInput(const QDBusMessage& message);
  bool HandleInputAction(const QDBusMessage& message);
  bool HandleSeat(const QDBusMessage& message);
  bool HandleTheme(const QDBusMessage& message);
  bool HandleEffect(const QDBusMessage& message);
  bool HandleMouseFinder(const QDBusMessage& message);
  bool HandlePlugin(const QDBusMessage& message);
  bool HandleScreenshot(const QDBusMessage& message);
  bool HandleWatermark(const QDBusMessage& message);
  bool HandleDeepinWm(const QDBusMessage& message);
  bool HandlePresentWindows(const QDBusMessage& message);
  bool HandleKdeInput(const QDBusMessage& message);
  bool HandleClipboard(const QDBusMessage& message);
  bool HandleKGlobalAccel(const QDBusMessage& message);
  bool HandleKGlobalAccelComponent(const QDBusMessage& message);
  bool HandleProperties(const QDBusMessage& message);

  InputDevice* FindInput(const QString& name) const;
  wlr_output* FindOutput(const QString& name) const;
  bool ConfigureOutput(wlr_output* output, bool* enabled, double* scale,
                       wl_output_transform* transform, int width, int height,
                       int refresh_mhz);
  bool MoveOutput(wlr_output* output, int x, int y);
  QString PrimaryOutputName() const;
  void SetPrimaryOutputName(const QString& name);
  void PersistOutput(wlr_output* output);
  bool ApplyOutputColor(wlr_output* output, int brightness,
                        int color_temperature);
  void RebuildWatermark(Watermark* watermark);
  void RebuildWatermarks();
  Watermark* FindWatermark(const QString& id) const;
  void SetShowDesktop(bool enabled);
  QString CaptureScreenshot(const QString& output_name, const wlr_box* area,
                            bool include_cursor, bool unscaled);
  bool SetGtkSetting(const QString& schema, const QString& key,
                     const QString& value) const;
  QJsonObject EffectOptions(const QString& name) const;
  bool EffectEnabled(const QString& name) const;
  void LoadEffectState();
  void ApplyBlurEffect();
  void ApplyEffectState(const QString& name);
  void SetMouseFinderEnabled(bool enabled, bool mirror_to_gsettings);
  void SetupMouseSettings();

  GlobalShortcutComponent* FindComponent(const QString& name);
  const GlobalShortcutComponent* FindComponent(const QString& name) const;
  void ActivateGlobalShortcut(GlobalShortcutComponent* component,
                              GlobalShortcut* shortcut);
  void DeactivateGlobalShortcut(GlobalShortcut* shortcut);
  static QString ComponentPath(const QString& name);

  core::CompositorPrivate* compositor_;
  input::KeyBindingManager* key_bindings_;
  QDBusConnection session_bus_;
  QDBusConnection system_bus_;
  QStringList owned_names_;
  QStringList registered_paths_;
  QString config_path_;
  QJsonObject config_;
  QJsonObject system_config_;
  std::vector<std::unique_ptr<InputDevice>> inputs_;
  std::vector<std::unique_ptr<GlobalShortcutComponent>> components_;
  QSet<quintptr> show_desktop_windows_;
  std::vector<std::unique_ptr<Watermark>> watermarks_;
  QHash<wlr_output*, wlr_color_transform*> output_color_transforms_;
  QHash<QString, quint64> custom_binding_ids_;
  QSet<QString> ukui_shortcut_services_;
  QHash<QString, QStringList> ukui_shortcut_blacklists_;
  QHash<QString, QStringList> ukui_shortcut_whitelists_;
  QSet<QString> ukui_shortcut_block_all_;
  // Runtime effect state. EnableEffect only changes this, like GXWM; the
  // persisted "enabled" option is read at startup.
  QHash<QString, bool> effect_enabled_;
#ifdef FLAKEWM_HAS_QGSETTINGS
  // org.ukui.peripherals-mouse, whose shake-cursor key mirrors MouseFinder.
  std::unique_ptr<QGSettings> mouse_settings_;
#endif
  bool show_desktop_ = false;
  bool shortcuts_blocked_ = false;
  int clipboard_pid_ = 0;
  int primary_selection_pid_ = 0;
};

}  // namespace dbus
}  // namespace flakewm

#endif  // SRC_DBUS_WLCOM_WLCOM_DBUS_MANAGER_H_
