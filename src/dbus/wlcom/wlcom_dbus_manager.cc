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

WlcomDbusManager::WlcomDbusManager(core::CompositorPrivate* compositor,
                                   input::KeyBindingManager* key_bindings)
    : compositor_(compositor),
      key_bindings_(key_bindings),
      session_bus_(QDBusConnection::sessionBus()),
      system_bus_(QDBusConnection::systemBus()) {
  config_path_ =
      QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) +
      QStringLiteral("/flakewm/config.json");

  qDBusRegisterMetaType<types::StringPair>();
  qDBusRegisterMetaType<types::Effect>();
  qDBusRegisterMetaType<types::Plugin>();
  qDBusRegisterMetaType<types::View>();
  qDBusRegisterMetaType<types::State>();
  qDBusRegisterMetaType<types::NamedBool>();
  qDBusRegisterMetaType<types::Binding>();
  qDBusRegisterMetaType<types::Input>();
  qDBusRegisterMetaType<types::KeySequence>();
  qDBusRegisterMetaType<types::ShortcutInfo>();
  qDBusRegisterMetaType<QList<types::StringPair>>();
  qDBusRegisterMetaType<QList<types::Effect>>();
  qDBusRegisterMetaType<QList<types::Plugin>>();
  qDBusRegisterMetaType<QList<types::View>>();
  qDBusRegisterMetaType<QList<types::State>>();
  qDBusRegisterMetaType<QList<types::NamedBool>>();
  qDBusRegisterMetaType<QList<types::Binding>>();
  qDBusRegisterMetaType<QList<types::Input>>();
  qDBusRegisterMetaType<QList<types::KeySequence>>();
  qDBusRegisterMetaType<QList<types::ShortcutInfo>>();
  qDBusRegisterMetaType<QMap<QString, QString>>();
  qDBusRegisterMetaType<QList<QStringList>>();
}

WlcomDbusManager::~WlcomDbusManager() {
  if (compositor_ != nullptr && compositor_->protocol_manager_ != nullptr)
    compositor_->protocol_manager_->SetGestureHandler({});
  if (key_bindings_ != nullptr) key_bindings_->CancelGrab();
  const QSet<QString> shortcut_services = ukui_shortcut_services_;
  for (const QString& service : shortcut_services)
    RemoveUkuiShortcutService(service);
  for (auto& component : components_)
    for (auto& shortcut : component->shortcuts)
      DeactivateGlobalShortcut(&shortcut);
  for (auto& watermark : watermarks_) {
    for (Watermark::Entry& entry : watermark->entries) {
      if (entry.scene != nullptr) wlr_scene_node_destroy(&entry.scene->node);
      if (entry.buffer != nullptr) wlr_buffer_drop(entry.buffer);
    }
  }
  watermarks_.clear();
  for (wlr_color_transform* transform : output_color_transforms_)
    wlr_color_transform_unref(transform);
  output_color_transforms_.clear();
  for (const QString& path : registered_paths_)
    session_bus_.unregisterObject(path);
  for (const QString& name : owned_names_) session_bus_.unregisterService(name);
}

bool WlcomDbusManager::Start() {
  if (!session_bus_.isConnected()) {
    ABSL_LOG(WARNING) << "Session D-Bus is unavailable";
    return false;
  }
  LoadConfig();
  LoadInputActions();
  SetupUkuiShortcutIntegration();
  auto built_in = std::make_unique<GlobalShortcutComponent>();
  built_in->unique_name = QStringLiteral("gxde-wlcom");
  built_in->friendly_name = QStringLiteral("GXDE Wlcom");
  built_in->path = ComponentPath(built_in->unique_name);
  if (key_bindings_ != nullptr) {
    for (const auto& binding : key_bindings_->Bindings()) {
      QString portable = QString::fromStdString(binding.shortcut);
      if (portable.endsWith(QStringLiteral(":no"), Qt::CaseInsensitive))
        portable.chop(3);
      const QKeySequence sequence =
          QKeySequence::fromString(portable, QKeySequence::PortableText);
      const qint32 key = sequence.isEmpty() ? 0 : sequence[0].toCombined();
      built_in->shortcuts.push_back(
          {QString::fromStdString(binding.description),
           QString::fromStdString(binding.description), key, key, true, false});
    }
  }
  components_.push_back(std::move(built_in));
  LoadEffectState();
  const bool core_name = RegisterNames();
  const bool objects = RegisterObjects();
  if (compositor_ != nullptr && compositor_->protocol_manager_ != nullptr) {
    compositor_->protocol_manager_->SetGestureHandler(
        [this](const char* type, const char* device, const char* direction,
               uint32_t fingers, const char* edge, const char* stage,
               const char* follow_direction, double dx, double dy) {
          return HandleGestureAction(type, device, direction, fingers, edge,
                                     stage, follow_direction, dx, dy);
        });
    ApplyBlurEffect();
  }
  return core_name && objects;
}

void WlcomDbusManager::UpdateActivationEnvironment() {
  if (!session_bus_.isConnected()) return;

  QMap<QString, QString> environment;
  QStringList assignments;
  for (const char* raw_name :
       {"WAYLAND_DISPLAY", "DISPLAY", "XDG_CURRENT_DESKTOP",
        "XDG_SESSION_DESKTOP", "XDG_SESSION_TYPE"}) {
    const QString name = QString::fromLatin1(raw_name);
    const QString value = qEnvironmentVariable(raw_name);
    if (value.isEmpty()) continue;
    environment.insert(name, value);
    assignments << name + QLatin1Char('=') + value;
  }
  if (environment.isEmpty()) return;

  QDBusMessage dbus = QDBusMessage::createMethodCall(
      QStringLiteral("org.freedesktop.DBus"),
      QStringLiteral("/org/freedesktop/DBus"),
      QStringLiteral("org.freedesktop.DBus"),
      QStringLiteral("UpdateActivationEnvironment"));
  dbus << QVariant::fromValue(environment);
  session_bus_.send(dbus);

  QDBusMessage systemd = QDBusMessage::createMethodCall(
      QStringLiteral("org.freedesktop.systemd1"),
      QStringLiteral("/org/freedesktop/systemd1"),
      QStringLiteral("org.freedesktop.systemd1.Manager"),
      QStringLiteral("SetEnvironment"));
  systemd << assignments;
  session_bus_.send(systemd);
}

wlr_color_transform* WlcomDbusManager::OutputColorTransform(
    wlr_output* output) const {
  return output_color_transforms_.value(output, nullptr);
}

bool WlcomDbusManager::ApplyOutputColor(wlr_output* output, int brightness,
                                        int color_temperature) {
  if (output == nullptr || !output->enabled) return false;
  brightness = std::clamp(brightness, 0, 100);
  color_temperature = std::clamp(color_temperature, 1000, 25100);

  // The neutral wlcom values must leave the client pixels untouched.  Even an
  // identity matrix would opt the output into wlroots' color-transform path,
  // which can introduce an unnecessary conversion/quantization round trip.
  if (brightness == 100 && color_temperature == 6500) {
    if (wlr_color_transform* old = output_color_transforms_.take(output);
        old != nullptr) {
      wlr_color_transform_unref(old);
    }
    wlr_output_schedule_frame(output);
    return true;
  }

  const double temperature = color_temperature / 100.0;
  auto channel = [](double value) {
    return static_cast<float>(std::clamp(value, 0.0, 255.0) / 255.0);
  };
  auto temperature_channels = [channel](double value) {
    return std::array<float, 3>{
        channel(value <= 66.0
                    ? 255.0
                    : 329.698727446 * std::pow(value - 60.0, -0.1332047592)),
        channel(value <= 66.0
                    ? 99.4708025861 * std::log(value) - 161.1195681661
                    : 288.1221695283 * std::pow(value - 60.0, -0.0755148492)),
        channel(value >= 66.0
                    ? 255.0
                    : (value <= 19.0 ? 0.0
                                     : 138.5177312231 * std::log(value - 10.0) -
                                           305.0447927307)),
    };
  };
  const std::array<float, 3> neutral = temperature_channels(65.0);
  const std::array<float, 3> channels = temperature_channels(temperature);
  const float red = std::clamp(channels[0] / neutral[0], 0.0F, 1.0F);
  const float green = std::clamp(channels[1] / neutral[1], 0.0F, 1.0F);
  const float blue = std::clamp(channels[2] / neutral[2], 0.0F, 1.0F);
  const float level = brightness / 100.0F;
  const float matrix[9] = {red * level, 0.0F, 0.0F, 0.0F,        green * level,
                           0.0F,        0.0F, 0.0F, blue * level};
  wlr_color_transform* transform = wlr_color_transform_init_matrix(matrix);
  if (transform == nullptr) return false;
  if (wlr_color_transform* old = output_color_transforms_.take(output);
      old != nullptr) {
    wlr_color_transform_unref(old);
  }
  output_color_transforms_.insert(output, transform);
  wlr_output_schedule_frame(output);
  return true;
}

QString WlcomDbusManager::PrimaryOutputName() const {
  const QJsonObject outputs =
      config_.value(QStringLiteral("outputs")).toObject();
  for (auto it = outputs.begin(); it != outputs.end(); ++it)
    if (it.value().toObject().value(QStringLiteral("primary")).toBool())
      return it.key();
  return {};
}

void WlcomDbusManager::SetPrimaryOutputName(const QString& name) {
  QJsonObject outputs = config_.value(QStringLiteral("outputs")).toObject();
  if (!name.isEmpty() && !outputs.contains(name)) outputs[name] = QJsonObject{};
  for (auto it = outputs.begin(); it != outputs.end(); ++it) {
    QJsonObject output = it.value().toObject();
    output[QStringLiteral("primary")] = it.key() == name;
    it.value() = output;
  }
  config_[QStringLiteral("outputs")] = outputs;
}

void WlcomDbusManager::PersistOutput(wlr_output* output) {
  if (output == nullptr) return;
  QJsonObject outputs = config_.value(QStringLiteral("outputs")).toObject();
  QJsonObject state = outputs.value(QString::fromUtf8(output->name)).toObject();
  wlr_box box = {};
  if (output->enabled)
    wlr_output_layout_get_box(compositor_->output_layout_, output, &box);
  const QByteArray identity =
      QByteArray(output->make == nullptr ? "" : output->make) + '\0' +
      QByteArray(output->model == nullptr ? "" : output->model) + '\0' +
      QByteArray(output->serial == nullptr ? "" : output->serial) + '\0' +
      QByteArray(output->name == nullptr ? "" : output->name);
  if (!state.contains(QStringLiteral("uuid"))) {
    const QByteArray digest =
        QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex();
    state[QStringLiteral("uuid")] =
        QStringLiteral("%1-%2-%3-%4-%5")
            .arg(QString::fromLatin1(digest.mid(0, 8)),
                 QString::fromLatin1(digest.mid(8, 4)),
                 QString::fromLatin1(digest.mid(12, 4)),
                 QString::fromLatin1(digest.mid(16, 4)),
                 QString::fromLatin1(digest.mid(20, 12)));
  }
  state[QStringLiteral("enabled")] = output->enabled;
  state[QStringLiteral("width")] = output->width;
  state[QStringLiteral("height")] = output->height;
  state[QStringLiteral("refresh")] = output->refresh;
  state[QStringLiteral("scale")] = output->scale;
  state[QStringLiteral("transform")] = static_cast<int>(output->transform);
  if (output->enabled || !state.contains(QStringLiteral("lx"))) {
    state[QStringLiteral("lx")] = box.x;
    state[QStringLiteral("ly")] = box.y;
  }
  if (!state.contains(QStringLiteral("brightness")))
    state[QStringLiteral("brightness")] = 100;
  if (!state.contains(QStringLiteral("color_temp")))
    state[QStringLiteral("color_temp")] = 6500;
  state[QStringLiteral("primary")] =
      PrimaryOutputName() == QString::fromUtf8(output->name);
  outputs[QString::fromUtf8(output->name)] = state;
  config_[QStringLiteral("outputs")] = outputs;
}

void WlcomDbusManager::AddOutput(wlr_output* output) {
  if (output == nullptr) return;
  const QString name = QString::fromUtf8(output->name);
  QJsonObject saved = config_.value(QStringLiteral("outputs"))
                          .toObject()
                          .value(name)
                          .toObject();
  if (PrimaryOutputName().isEmpty()) SetPrimaryOutputName(name);
  if (!saved.isEmpty()) {
    bool enabled = saved.value(QStringLiteral("enabled")).toBool(true);
    double scale = saved.value(QStringLiteral("scale")).toDouble(output->scale);
    wl_output_transform transform = static_cast<wl_output_transform>(
        saved.value(QStringLiteral("transform")).toInt(output->transform));
    const int width = saved.value(QStringLiteral("width")).toInt();
    const int height = saved.value(QStringLiteral("height")).toInt();
    const int refresh = saved.value(QStringLiteral("refresh")).toInt();
    ConfigureOutput(output, &enabled, &scale, &transform, width, height,
                    refresh);
    if (enabled)
      MoveOutput(output, saved.value(QStringLiteral("lx")).toInt(),
                 saved.value(QStringLiteral("ly")).toInt());
  }
  saved = config_.value(QStringLiteral("outputs"))
              .toObject()
              .value(name)
              .toObject();
  ApplyOutputColor(output, saved.value(QStringLiteral("brightness")).toInt(100),
                   saved.value(QStringLiteral("color_temp")).toInt(6500));
  PersistOutput(output);
  SaveConfig();
  RebuildWatermarks();
}

void WlcomDbusManager::RemoveOutput(wlr_output* output) {
  if (wlr_color_transform* transform = output_color_transforms_.take(output);
      transform != nullptr) {
    wlr_color_transform_unref(transform);
  }
  for (auto& watermark : watermarks_) {
    auto& entries = watermark->entries;
    for (auto it = entries.begin(); it != entries.end();) {
      if (it->output != output) {
        ++it;
        continue;
      }
      if (it->scene != nullptr) wlr_scene_node_destroy(&it->scene->node);
      if (it->buffer != nullptr) wlr_buffer_drop(it->buffer);
      it = entries.erase(it);
    }
  }
}

bool WlcomDbusManager::RegisterNames() {
  static constexpr std::array<const char*, 10> names = {
      kKylinService,
      "top.gxde.Wlcom",
      "top.gxde.Wlcom.Screen",
      "top.gxde.Wlcom.Screenshot",
      "top.gxde.Wlcom.Theme",
      "top.gxde.Wlcom.WindowBtn",
      "top.gxde.Wlcom.WindowCorner",
      "org.kde.KWin",
      "org.ukui.KWin",
      "org.kde.KWin.PresentWindows"};
  bool core = false;
  for (const char* raw_name : names) {
    const QString name = QString::fromLatin1(raw_name);
    if (session_bus_.registerService(name)) {
      owned_names_ << name;
      if (name == QString::fromLatin1(kKylinService)) core = true;
    } else {
      ABSL_LOG(WARNING) << "Cannot own D-Bus name " << raw_name << ": "
                        << session_bus_.lastError().message().toStdString();
    }
  }
  for (const char* raw_name : {"org.kde.kglobalaccel", "com.deepin.wm"}) {
    const QString name = QString::fromLatin1(raw_name);
    if (session_bus_.registerService(name))
      owned_names_ << name;
    else
      ABSL_LOG(WARNING) << "Cannot own D-Bus name " << raw_name;
  }
  return core;
}

bool WlcomDbusManager::RegisterObjects() {
  struct Object {
    const char* path;
    QDBusConnection::VirtualObjectRegisterOption option;
  };
  static constexpr Object objects[] = {
      {"/com/kylin/Wlcom", QDBusConnection::SingleNode},
      {"/com/kylin/Wlcom/Output", QDBusConnection::SingleNode},
      {"/com/kylin/Wlcom/View", QDBusConnection::SingleNode},
      {"/com/kylin/Wlcom/Input", QDBusConnection::SingleNode},
      {"/com/kylin/Wlcom/InputAction", QDBusConnection::SingleNode},
      {"/com/kylin/Wlcom/Seat", QDBusConnection::SingleNode},
      {"/com/kylin/Wlcom/Theme", QDBusConnection::SingleNode},
      {"/com/kylin/Wlcom/Effect", QDBusConnection::SingleNode},
      {"/com/kylin/Wlcom/Plugin", QDBusConnection::SingleNode},
      {"/com/kylin/Wlcom/Watermark", QDBusConnection::SingleNode},
      {"/top/gxde/Wlcom/Screen", QDBusConnection::SingleNode},
      {"/top/gxde/Wlcom/Effect", QDBusConnection::SingleNode},
      {"/top/gxde/Wlcom/Screenshot", QDBusConnection::SingleNode},
      {"/top/gxde/Wlcom/Theme", QDBusConnection::SingleNode},
      {"/top/gxde/Wlcom/WindowBtn", QDBusConnection::SingleNode},
      {"/top/gxde/Wlcom/WindowCorner", QDBusConnection::SingleNode},
      {"/Screenshot", QDBusConnection::SingleNode},
      {"/Watermark", QDBusConnection::SingleNode},
      {"/Clipboard", QDBusConnection::SingleNode},
      {"/com/deepin/wm", QDBusConnection::SingleNode},
      {"/org/kde/KWin/PresentWindows", QDBusConnection::SingleNode},
      {"/org/kde/KWin/InputDevice", QDBusConnection::SubPath},
      {"/kglobalaccel", QDBusConnection::SingleNode},
      {"/component", QDBusConnection::SubPath},
  };
  bool ok = true;
  for (const Object& object : objects) {
    const QString path = QString::fromLatin1(object.path);
    if (session_bus_.registerVirtualObject(path, this, object.option)) {
      registered_paths_ << path;
    } else {
      ok = false;
      ABSL_LOG(WARNING) << "Cannot register D-Bus object " << object.path
                        << ": "
                        << session_bus_.lastError().message().toStdString();
    }
  }
  return ok;
}

void WlcomDbusManager::LoadConfig() {
  QFile system_file(QStringLiteral("/etc/gxde-wlcom/config.json"));
  if (system_file.open(QIODevice::ReadOnly)) {
    const QJsonDocument document =
        QJsonDocument::fromJson(system_file.readAll());
    if (document.isObject()) system_config_ = document.object();
  }

  QFile file(config_path_);
  if (!file.open(QIODevice::ReadOnly)) {
    const QString config_root =
        QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    bool legacy_config_found = false;
    for (const QString& legacy_path :
         {config_root + QStringLiteral("/flakewm/dbus.json"),
          config_root + QStringLiteral("/gxde-wlcom/config.json")}) {
      file.setFileName(legacy_path);
      if (file.open(QIODevice::ReadOnly)) {
        legacy_config_found = true;
        break;
      }
    }
    if (!legacy_config_found) return;
  }
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
  if (document.isObject()) config_ = document.object();

  // Migrate the short-lived FlakeWM draft schema into the real GXWM schema.
  if (config_.contains(QStringLiteral("effects")) &&
      !config_.contains(QStringLiteral("Effects")))
    config_[QStringLiteral("Effects")] =
        config_.take(QStringLiteral("effects"));

  QJsonObject views = config_.value(QStringLiteral("Views")).toObject();
  for (const QString& key :
       {QStringLiteral("minimize_effect"), QStringLiteral("view_adsorption"),
        QStringLiteral("csd_round_corner")}) {
    if (config_.contains(key)) views[key] = config_.take(key);
  }
  if (!views.isEmpty()) config_[QStringLiteral("Views")] = views;

  QJsonObject seats = config_.value(QStringLiteral("Seats")).toObject();
  QJsonObject seat = seats.value(QStringLiteral("seat0")).toObject();
  for (const QString& key :
       {QStringLiteral("cursor_theme"), QStringLiteral("cursor_size"),
        QStringLiteral("keyboard_lock_mode")}) {
    if (config_.contains(key)) seat[key] = config_.take(key);
  }
  if (!seat.isEmpty()) {
    seats[QStringLiteral("seat0")] = seat;
    config_[QStringLiteral("Seats")] = seats;
  }

  if (config_.contains(QStringLiteral("input_actions")) &&
      !config_.contains(QStringLiteral("InputAction"))) {
    config_[QStringLiteral("InputAction")] =
        QJsonObject{{QStringLiteral("keyboard"),
                     config_.take(QStringLiteral("input_actions"))}};
  }

  if (config_.contains(QStringLiteral("primary_output"))) {
    const QString primary =
        config_.take(QStringLiteral("primary_output")).toString();
    QJsonObject outputs = config_.value(QStringLiteral("outputs")).toObject();
    for (auto it = outputs.begin(); it != outputs.end(); ++it) {
      QJsonObject output = it.value().toObject();
      output[QStringLiteral("primary")] = it.key() == primary;
      it.value() = output;
    }
    config_[QStringLiteral("outputs")] = outputs;
  }
}

void WlcomDbusManager::SaveConfig() const {
  QDir().mkpath(QFileInfo(config_path_).absolutePath());
  QFile file(config_path_);
  if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    file.write(QJsonDocument(config_).toJson(QJsonDocument::Indented));
}

QString WlcomDbusManager::introspect(const QString& path) const {
  if (path == QStringLiteral("/com/kylin/Wlcom")) {
    return Interface("com.kylin.Wlcom", Method("SetLogLevel", "u") +
                                            Method("PrintConfig", "", "s") +
                                            Method("TrimMemory", "b"));
  }
  if (path == QStringLiteral("/com/kylin/Wlcom/Output")) {
    return Interface("com.kylin.Wlcom.Output",
                     Method("ListAllOutputs", "", "a(ss)") +
                         Method("SetBrightness", "s u") +
                         Method("SetColortemp", "s u"));
  }
  if (path == QStringLiteral("/top/gxde/Wlcom/Screen")) {
    return Interface(
        "top.gxde.Wlcom.Screen",
        Method("GetCursorOutput", "", "s") + Method("SetScaleRatio", "d") +
            Method("SetResolutionWRefreshRate", "i i i") +
            Method("SetScreenBrightness", "s i") + Method("RotateScreen", "i") +
            Method("SetScreenScale", "s d") +
            Method("SetScreenResolution", "s i i i") +
            Method("SetScreenRotation", "s i") +
            Method("SetScreenEnabled", "s b") +
            Method("SetPrimaryScreen", "s") +
            Method("SetScreenPosition", "s i i") +
            Method("SetScreenMode", "u s") +
            Method("SetScreenLayout", "a(sii)"));
  }
  if (path == QStringLiteral("/com/kylin/Wlcom/View")) {
    return Interface("com.kylin.Wlcom.View",
                     Method("GetViewAdsorption", "", "u") +
                         Method("SetViewAdsorption", "u") +
                         Method("SetCSDRoundCorner", "b") +
                         Method("GetMinimizeEffect", "", "u") +
                         Method("SetMinimizeEffect", "u") +
                         Method("GetIsShowDesktop", "", "b") +
                         Method("SetShowDesktop", "b") +
                         Method("ToggleShowDesktop") +
                         Method("ListAllViews", "", "a(ssi)") +
                         Method("ListViewStates", "s", "a(sai)") +
                         Method("ListAllModes", "", "a(sb)"));
  }
  if (path == QStringLiteral("/com/deepin/wm")) {
    return Interface("com.deepin.wm", Method("PerformAction", "i") +
                                          Method("GetIsShowDesktop", "", "b") +
                                          Method("SetShowDesktop", "b"));
  }
  if (path == QStringLiteral("/org/kde/KWin/PresentWindows"))
    return Interface("org.kde.KWin.PresentWindows",
                     Method("presentWindows", "as"));
  if (path == QStringLiteral("/com/kylin/Wlcom/InputAction")) {
    return Interface("com.kylin.Wlcom.InputAction",
                     Method("ListAllActions", "", "a(ss)") +
                         Method("ListKeyBindings", "", "a(sss)") +
                         Method("AddAction", "s s s s s") +
                         Method("ControlAction", "s s") +
                         Method("GrabNextKey") + Method("CancelGrab") +
                         Signal("KeyEvent", "b s"));
  }
  if (path == QStringLiteral("/com/kylin/Wlcom/Input")) {
    QString body =
        Method("ListAllInputs", "", "a(su)") + Method("MapToOutput", "s s") +
        Method("ChangeSeat", "s s") + Method("GetSendEventsMode", "s", "u u") +
        Method("SetSendEventsMode", "s u") +
        Method("GetTapToClick", "s", "b b") +
        Method("EnableTapToClick", "s b") +
        Method("GetTapAndDrag", "s", "b b") +
        Method("EnableTapAndDrag", "s b") +
        Method("GetPointerSpeed", "s", "d d") +
        Method("SetPointerSpeed", "s d") +
        Method("GetAccelProfile", "s", "u u") +
        Method("SetAccelProfile", "s u") +
        Method("GetScrollMethod", "s", "u u") +
        Method("SetScrollMethod", "s u") +
        Method("GetDisableWhileTyping", "s", "b b") +
        Method("SetDisableWhileTyping", "s b") +
        Method("GetNaturalScroll", "s", "b b") +
        Method("EnableNaturalScroll", "s b") +
        Method("GetLeftHand", "s", "b b") + Method("EnableLeftHand", "s b") +
        Method("GetRepeatInfo", "s", "i i i i") +
        Method("SetRepeatInfo", "s i i") +
        Method("GetKeymap", "s", "s s s s s") +
        Method("SetKeymap", "s s s s s s") +
        Method("GetKeymapGroup", "s", "u") + Method("SetKeymapGroup", "s u") +
        Method("GetScrollFactor", "s", "d d") +
        Method("SetScrollFactor", "s d") +
        Method("GetDoubleClickTime", "s", "u u") +
        Method("SetDoubleClickTime", "s u") +
        Signal("KeymapGroupChanged", "s u");
    return Interface("com.kylin.Wlcom.Input", body);
  }
  if (path == QStringLiteral("/com/kylin/Wlcom/Seat")) {
    return Interface("com.kylin.Wlcom.Seat",
                     Method("ListAllSeats", "", "a(ss)") +
                         Method("SetCursor", "s s u") +
                         Method("SetLockKeysMode", "s u"));
  }
  if (path == QStringLiteral("/com/kylin/Wlcom/Theme")) {
    return Interface("com.kylin.Wlcom.Theme",
                     Method("PrintThemeConfig", "", "s") +
                         Method("SetWidgetTheme", "s u", "b") +
                         Method("SetIconTheme", "s", "b") +
                         Method("SetFont", "s i", "b") +
                         Method("SetAccentColor", "i", "b") +
                         Method("SetOpacity", "i", "b"));
  }
  if (path == QStringLiteral("/top/gxde/Wlcom/Theme"))
    return Interface("top.gxde.Wlcom.Theme", Method("SetGTK", "s", "b"));
  if (path == QStringLiteral("/top/gxde/Wlcom/WindowBtn"))
    return Interface("top.gxde.Wlcom.WindowBtn",
                     Method("GetGtkDecorationButtons", "", "b b b") +
                         Method("SetGtkDecorationButtons", "b b b", "b"));
  if (path == QStringLiteral("/top/gxde/Wlcom/WindowCorner"))
    return Interface(
        "top.gxde.Wlcom.WindowCorner",
        Method("GetCornerRadius", "", "i") +
            Method("SetCornerRadius", "i", "b") +
            Method("GetForceRoundCorner", "", "b") +
            Method("SetForceRoundCorner", "b", "b") +
            Method("GetForceRoundCornerExcludeLayerShell", "", "b") +
            Method("SetForceRoundCornerExcludeLayerShell", "b", "b"));
  if (path == QStringLiteral("/com/kylin/Wlcom/Effect") ||
      path == QStringLiteral("/top/gxde/Wlcom/Effect")) {
    const char* iface = path.startsWith(QStringLiteral("/top"))
                            ? "top.gxde.Wlcom.Effect"
                            : "com.kylin.Wlcom.Effect";
    return Interface(iface, Method("ListAllEffects", "", "a(sub)") +
                                Method("EnableEffect", "s b") +
                                Method("PrintEffectOptions", "s", "s") +
                                Method("SetEffectOption", "s s v"));
  }
  if (path == QStringLiteral("/com/kylin/Wlcom/Plugin"))
    return Interface("com.kylin.Wlcom.Plugin",
                     Method("ListAllPlugins", "b", "a(sbb)") +
                         Method("PrintPluginInfo", "s", "s s s u u") +
                         Method("PrintPluginConfig", "s", "s") +
                         Method("LoadPlugin", "s b") +
                         Method("EnablePlugin", "s b") +
                         Method("SetPluginOption", "s s v"));
  if (path == QStringLiteral("/Screenshot")) {
    return Interface("org.ukui.kwin.Screenshot",
                     Method("screenshotFullscreen", "", "s") +
                         Method("screenshotFull", "b b", "s") +
                         Method("screenshotOutput", "s b b", "s") +
                         Method("screenshotArea", "i i i i b b", "s")) +
           Interface("org.kde.kwin.Screenshot",
                     Method("screenshotFullscreen", "b", "s"));
  }
  if (path == QStringLiteral("/top/gxde/Wlcom/Screenshot"))
    return Interface("top.gxde.Wlcom.Screenshot",
                     Method("CopyFullscreenToClipboard"));
  if (path == QStringLiteral("/Watermark"))
    return Interface("org.ukui.kwin.Watermark",
                     Method("updateWatermark", "s d") +
                         Method("updateWatermarkEx", "s d i i i b"));
  if (path == QStringLiteral("/com/kylin/Wlcom/Watermark"))
    return Interface("com.kylin.Wlcom.Watermark",
                     Method("createWatermark", "s d i i i b", "s") +
                         Method("updateWatermark", "s s d i i i b", "b") +
                         Method("destroyWatermark", "s"));
  if (path == QStringLiteral("/Clipboard"))
    return Interface("org.kde.KWin.Clipboard",
                     Method("getClipboardSelectionPid", "", "i") +
                         Method("getPrimarySelectionPid", "", "i") +
                         Property("GetClipboardSelectionPid", "i", "read") +
                         Property("GetPrimarySelectionPid", "i", "read") +
                         Signal("clipboardSelectionPidChanged", "i") +
                         Signal("primarySelectionPidChanged", "i"));
  if (path == QStringLiteral("/org/kde/KWin/InputDevice"))
    return Interface("org.kde.KWin.InputDeviceManager",
                     Property("devicesSysNames", "as", "read"));
  if (path.startsWith(QStringLiteral("/org/kde/KWin/InputDevice/"))) {
    QString body;
    for (const auto& prop : std::to_array<std::pair<const char*, const char*>>(
             {{"pointer", "b"},
              {"keyboard", "b"},
              {"touchpad", "b"},
              {"touch", "b"},
              {"tabletTool", "b"},
              {"tabletPad", "b"},
              {"switchDevice", "b"},
              {"name", "s"},
              {"sysName", "s"},
              {"product", "i"},
              {"vendor", "i"},
              {"supportsDisableEvents", "b"},
              {"supportsDisableEventsOnExternalMouse", "b"},
              {"supportsPointerAcceleration", "b"},
              {"defaultPointerAcceleration", "d"},
              {"supportsLeftHanded", "b"},
              {"leftHandedEnabledByDefault", "b"},
              {"supportsPointerAccelerationProfileAdaptive", "b"},
              {"defaultPointerAccelerationProfileAdaptive", "b"},
              {"supportsNaturalScroll", "b"},
              {"naturalScrollEnabledByDefault", "b"},
              {"tapFingerCount", "b"},
              {"tapToClickEnabledByDefault", "b"},
              {"supportsScrollTwoFinger", "b"},
              {"scrollTwoFingerEnabledByDefault", "b"},
              {"supportsScrollEdge", "b"},
              {"scrollEdgeEnabledByDefault", "b"},
              {"supportsDisableWhileTyping", "b"},
              {"disableWhileTypingEnabledByDefault", "b"}}))
      body += Property(prop.first, prop.second, "read");
    for (const auto& prop : std::to_array<std::pair<const char*, const char*>>(
             {{"enabled", "b"},
              {"pointerAcceleration", "d"},
              {"leftHanded", "b"},
              {"pointerAccelerationProfileAdaptive", "b"},
              {"naturalScroll", "b"},
              {"tapToClick", "b"},
              {"scrollTwoFinger", "b"},
              {"scrollEdge", "b"},
              {"disableWhileTyping", "b"},
              {"outputName", "s"}}))
      body += Property(prop.first, prop.second, "readwrite");
    return Interface("org.kde.KWin.InputDevice", body);
  }
  if (path == QStringLiteral("/kglobalaccel")) {
    return Interface(
        "org.kde.KGlobalAccel",
        Signal("yourShortcutsChanged", "as a(ai)") +
            Method("actionList", "(ai)", "as") +
            Method("activateGlobalShortcutContext", "s s") +
            Method("allActionsForComponent", "as", "aas") +
            Method("allComponents", "", "ao") +
            Method("allMainComponents", "", "aas") +
            Method("blockGlobalShortcuts", "b") +
            Method("defaultShortcutKeys", "as", "a(ai)") +
            Method("doRegister", "as") + Method("getComponent", "s", "o") +
            Method("globalShortcutAvailable", "(ai) s", "b") +
            Method("globalShortcutsByKey", "(ai) i", "a(ssssssaiai)") +
            Method("setForeignShortcutKeys", "as a(ai)") +
            Method("setInactive", "as") +
            Method("setShortcutKeys", "as a(ai) u", "a(ai)") +
            Method("shortcutKeys", "as", "a(ai)") +
            Method("unregister", "s s", "b"));
  }
  if (path.startsWith(QStringLiteral("/component/")))
    return Interface("org.kde.kglobalaccel.Component",
                     Signal("globalShortcutPressed", "s s x") +
                         Property("friendlyName", "s", "read", "const") +
                         Property("uniqueName", "s", "read", "const") +
                         Method("allShortcutInfos", "s", "a(ssssssaiai)") +
                         Method("cleanUp", "", "b") +
                         Method("getShortcutContexts", "", "as") +
                         Method("invokeShortcut", "s s") +
                         Method("isActive", "", "b") +
                         Method("shortcutNames", "s", "as"));
  return {};
}

bool WlcomDbusManager::handleMessage(const QDBusMessage& message,
                                     const QDBusConnection&) {
  const QString interface = message.interface();
  if (interface == QStringLiteral("org.freedesktop.DBus.Properties"))
    return HandleProperties(message);
  if (interface == QStringLiteral("com.kylin.Wlcom"))
    return HandleCommon(message);
  if (interface == QStringLiteral("com.kylin.Wlcom.Output") ||
      interface == QStringLiteral("top.gxde.Wlcom.Screen"))
    return HandleOutput(message);
  if (interface == QStringLiteral("com.kylin.Wlcom.View"))
    return HandleView(message);
  if (interface == QStringLiteral("com.deepin.wm"))
    return HandleDeepinWm(message);
  if (interface == QStringLiteral("org.kde.KWin.PresentWindows"))
    return HandlePresentWindows(message);
  if (interface == QStringLiteral("com.kylin.Wlcom.Input"))
    return HandleInput(message);
  if (interface == QStringLiteral("com.kylin.Wlcom.InputAction"))
    return HandleInputAction(message);
  if (interface == QStringLiteral("com.kylin.Wlcom.Seat"))
    return HandleSeat(message);
  if (interface == QStringLiteral("com.kylin.Wlcom.Theme") ||
      interface.startsWith(QStringLiteral("top.gxde.Wlcom."))) {
    if (interface == QStringLiteral("top.gxde.Wlcom.Effect"))
      return HandleEffect(message);
    if (interface == QStringLiteral("top.gxde.Wlcom.Screenshot"))
      return HandleScreenshot(message);
    return HandleTheme(message);
  }
  if (interface == QStringLiteral("com.kylin.Wlcom.Effect"))
    return HandleEffect(message);
  if (interface == QStringLiteral("com.kylin.Wlcom.Plugin"))
    return HandlePlugin(message);
  if (interface == QStringLiteral("org.ukui.kwin.Screenshot") ||
      interface == QStringLiteral("org.kde.kwin.Screenshot"))
    return HandleScreenshot(message);
  if (interface == QStringLiteral("org.ukui.kwin.Watermark") ||
      interface == QStringLiteral("com.kylin.Wlcom.Watermark"))
    return HandleWatermark(message);
  if (interface == QStringLiteral("org.kde.KWin.InputDeviceManager") ||
      interface == QStringLiteral("org.kde.KWin.InputDevice"))
    return HandleKdeInput(message);
  if (interface == QStringLiteral("org.kde.KWin.Clipboard"))
    return HandleClipboard(message);
  if (interface == QStringLiteral("org.kde.KGlobalAccel"))
    return HandleKGlobalAccel(message);
  if (interface == QStringLiteral("org.kde.kglobalaccel.Component"))
    return HandleKGlobalAccelComponent(message);
  return false;
}

void WlcomDbusManager::Reply(const QDBusMessage& message,
                             const QVariantList& arguments) const {
  session_bus_.send(message.createReply(arguments));
}

void WlcomDbusManager::Error(const QDBusMessage& message, const QString& name,
                             const QString& text) const {
  session_bus_.send(message.createErrorReply(name, text));
}

void WlcomDbusManager::EmitSignal(const QString& path, const QString& interface,
                                  const QString& member,
                                  const QVariantList& arguments) {
  QDBusMessage signal = QDBusMessage::createSignal(path, interface, member);
  signal.setArguments(arguments);
  session_bus_.send(signal);
}

bool WlcomDbusManager::HandleCommon(const QDBusMessage& message) {
  if (message.member() == QStringLiteral("SetLogLevel")) {
    if (message.arguments().size() != 1) {
      Error(message, kInvalidArgs, "Expected uint32 level.");
      return true;
    }
    config_[QStringLiteral("log_level")] =
        static_cast<int>(message.arguments()[0].toUInt());
    SaveConfig();
    Reply(message);
    return true;
  }
  if (message.member() == QStringLiteral("PrintConfig")) {
    Reply(message, {QString::fromUtf8(
                       QJsonDocument(config_).toJson(QJsonDocument::Compact))});
    return true;
  }
  if (message.member() == QStringLiteral("TrimMemory")) {
    const bool enabled = message.arguments().value(0).toBool();
    if (enabled) malloc_trim(0);
    Reply(message);
    return true;
  }
  return false;
}

wlr_output* WlcomDbusManager::FindOutput(const QString& name) const {
  if (compositor_ == nullptr) return nullptr;
  for (const auto& state : compositor_->outputs_) {
    if (state->handle != nullptr &&
        QString::fromUtf8(state->handle->name) == name)
      return state->handle;
  }
  return nullptr;
}

bool WlcomDbusManager::ConfigureOutput(wlr_output* output, bool* enabled,
                                       double* scale,
                                       wl_output_transform* transform,
                                       int width, int height, int refresh_mhz) {
  if (output == nullptr) return false;
  wlr_output_state state;
  wlr_output_state_init(&state);
  if (enabled != nullptr) wlr_output_state_set_enabled(&state, *enabled);
  if (scale != nullptr)
    wlr_output_state_set_scale(&state, static_cast<float>(*scale));
  if (transform != nullptr) wlr_output_state_set_transform(&state, *transform);
  if (width > 0 && height > 0) {
    wlr_output_mode* match = nullptr;
    wlr_output_mode* mode = nullptr;
    wl_list_for_each(mode, &output->modes, link) {
      if (mode->width == width && mode->height == height &&
          (refresh_mhz <= 0 || std::abs(mode->refresh - refresh_mhz) <= 500)) {
        if (match == nullptr || mode->refresh > match->refresh) match = mode;
      }
    }
    if (match == nullptr) {
      wlr_output_state_finish(&state);
      return false;
    }
    wlr_output_state_set_mode(&state, match);
  }
  const bool committed = wlr_output_commit_state(output, &state);
  wlr_output_state_finish(&state);
  if (!committed) return false;
  if (enabled != nullptr) {
    wlr_output_layout_output* layout =
        wlr_output_layout_get(compositor_->output_layout_, output);
    if (*enabled && layout == nullptr) {
      layout = wlr_output_layout_add_auto(compositor_->output_layout_, output);
      core::CompositorPrivate::Output* wrapper =
          compositor_->FindOutput(output);
      if (layout != nullptr && wrapper != nullptr &&
          wrapper->scene_output != nullptr)
        wlr_scene_output_layout_add_output(compositor_->scene_layout_, layout,
                                           wrapper->scene_output);
    } else if (!*enabled && layout != nullptr) {
      wlr_output_layout_remove(compositor_->output_layout_, output);
    }
  }
  if (core::CompositorPrivate::Output* wrapper =
          compositor_->FindOutput(output))
    compositor_->ArrangeLayers(wrapper);
  compositor_->UpdateQtFrameInterval();
  if (compositor_->protocol_manager_ != nullptr)
    compositor_->protocol_manager_->UpdateOutputs();
  PersistOutput(output);
  RebuildWatermarks();
  return true;
}

bool WlcomDbusManager::MoveOutput(wlr_output* output, int x, int y) {
  if (output == nullptr || !output->enabled) return false;
  wlr_output_layout_output* layout =
      wlr_output_layout_add(compositor_->output_layout_, output, x, y);
  if (layout == nullptr) return false;
  core::CompositorPrivate::Output* wrapper = compositor_->FindOutput(output);
  if (wrapper != nullptr && wrapper->scene_output != nullptr)
    wlr_scene_output_layout_add_output(compositor_->scene_layout_, layout,
                                       wrapper->scene_output);
  if (wrapper != nullptr) compositor_->ArrangeLayers(wrapper);
  if (compositor_->protocol_manager_ != nullptr)
    compositor_->protocol_manager_->UpdateOutputs();
  PersistOutput(output);
  RebuildWatermarks();
  return true;
}

}  // namespace dbus
}  // namespace flakewm
