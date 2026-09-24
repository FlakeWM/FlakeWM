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

#include <optional>

#include "src/dbus/wlcom/wlcom_dbus_manager_p.h"

namespace flakewm {
namespace dbus {

bool WlcomDbusManager::SetGtkSetting(const QString& schema, const QString& key,
                                     const QString& value) const {
  return SetAvailableGtkSettings({{schema, key}}, value);
}

bool WlcomDbusManager::HandleTheme(const QDBusMessage& message) {
  const QVariantList args = message.arguments();
  QJsonObject theme = config_.value(QStringLiteral("theme")).toObject();
  auto persist = [&]() {
    config_[QStringLiteral("theme")] = theme;
    SaveConfig();
  };
  if (message.member() == QStringLiteral("PrintThemeConfig")) {
    Reply(message, {QString::fromUtf8(
                       QJsonDocument(theme).toJson(QJsonDocument::Compact))});
    return true;
  }
  if (message.member() == QStringLiteral("SetWidgetTheme")) {
    if (args.size() != 2 || args[0].toString().isEmpty()) {
      Error(message, kInvalidArgs, "Invalid theme.");
      return true;
    }
    theme[QStringLiteral("type")] = static_cast<int>(args[1].toUInt());
    persist();
    Reply(message, {true});
    return true;
  }
  if (message.member() == QStringLiteral("SetIconTheme")) {
    QString name = args.value(0).toString();
    const bool valid = !name.isEmpty() && !name.contains(QDir::separator()) &&
                       name != QStringLiteral("hicolor");
    const bool ok =
        valid &&
        SetAvailableGtkSettings({{QStringLiteral("org.gnome.desktop.interface"),
                                  QStringLiteral("icon-theme")}},
                                name);
    if (ok) {
      theme[QStringLiteral("icon_theme_name")] = args[0].toString();
      persist();
    }
    Reply(message, {ok});
    return true;
  }
  if (message.member() == QStringLiteral("SetGTK")) {
    const QString name = args.value(0).toString();
    const bool ok =
        GtkThemeInstalled(name) &&
        SetAvailableGtkSettings({{QStringLiteral("org.ukui.style"),
                                  QStringLiteral("widget-theme-name")},
                                 {QStringLiteral("org.gnome.desktop.interface"),
                                  QStringLiteral("gtk-theme")}},
                                name);
    Reply(message, {ok});
    return true;
  }
  if (message.member() == QStringLiteral("SetFont")) {
    const QString name = args.value(0).toString();
    const int size = args.value(1).toInt();
    if (args.size() != 2 || name.isEmpty() || size <= 0) {
      Reply(message, {false});
      return true;
    }
    theme[QStringLiteral("font_name")] = name;
    theme[QStringLiteral("font_size")] = size;
    persist();
    Reply(message, {true});
    return true;
  }
  if (message.member() == QStringLiteral("SetAccentColor")) {
    theme[QStringLiteral("accent_color")] = args.value(0).toInt();
    persist();
    Reply(message, {true});
    return true;
  }
  if (message.member() == QStringLiteral("SetOpacity")) {
    theme[QStringLiteral("opacity")] =
        std::clamp(args.value(0).toInt(), 0, 100);
    persist();
    Reply(message, {true});
    return true;
  }
  if (message.member() == QStringLiteral("GetGtkDecorationButtons")) {
    Reply(message,
          {theme.value(QStringLiteral("gtk_decoration_minimize")).toBool(true),
           theme.value(QStringLiteral("gtk_decoration_maximize")).toBool(true),
           theme.value(QStringLiteral("gtk_decoration_close")).toBool(true)});
    return true;
  }
  if (message.member() == QStringLiteral("SetGtkDecorationButtons")) {
    if (args.size() != 3) {
      Reply(message, {false});
      return true;
    }
    QStringList right;
    if (args[0].toBool()) right << QStringLiteral("minimize");
    if (args[1].toBool()) right << QStringLiteral("maximize");
    if (args[2].toBool()) right << QStringLiteral("close");
    const QString layout = QStringLiteral(":%1").arg(right.join(','));
    const bool ok =
        SetGtkSetting(QStringLiteral("org.gnome.desktop.wm.preferences"),
                      QStringLiteral("button-layout"), layout);
    if (ok) {
      theme[QStringLiteral("gtk_decoration_minimize")] = args[0].toBool();
      theme[QStringLiteral("gtk_decoration_maximize")] = args[1].toBool();
      theme[QStringLiteral("gtk_decoration_close")] = args[2].toBool();
      persist();
    }
    Reply(message, {ok});
    return true;
  }
  if (message.member() == QStringLiteral("GetCornerRadius")) {
    Reply(message, {theme.value(QStringLiteral("corner_radius")).toInt(8)});
    return true;
  }
  if (message.member() == QStringLiteral("SetCornerRadius")) {
    const int radius = args.value(0).toInt();
    theme[QStringLiteral("corner_radius")] = radius;
    persist();
    Reply(message, {true});
    return true;
  }
  if (message.member() == QStringLiteral("GetForceRoundCorner")) {
    Reply(message,
          {theme.value(QStringLiteral("force_round_corner")).toBool(false)});
    return true;
  }
  if (message.member() == QStringLiteral("SetForceRoundCorner")) {
    theme[QStringLiteral("force_round_corner")] = args.value(0).toBool();
    persist();
    Reply(message, {true});
    return true;
  }
  if (message.member() ==
      QStringLiteral("GetForceRoundCornerExcludeLayerShell")) {
    Reply(message,
          {theme.value(QStringLiteral("force_round_corner_exclude_layer_shell"))
               .toBool(false)});
    return true;
  }
  if (message.member() ==
      QStringLiteral("SetForceRoundCornerExcludeLayerShell")) {
    theme[QStringLiteral("force_round_corner_exclude_layer_shell")] =
        args.value(0).toBool();
    persist();
    Reply(message, {true});
    return true;
  }
  return false;
}

namespace {

enum class EffectOptionType : uint8_t { kBool, kInt, kDouble, kString };

struct EffectOptionDefault {
  const char* key;
  EffectOptionType type;
  int value;
};

struct EffectDefinition {
  const char* name;
  quint32 priority;
  bool enabled;
  std::array<EffectOptionDefault, 2> options;
  size_t option_count;
};

// GXWM's UI effect registry
constexpr int kMaxZoomScale = 16;
constexpr std::array kEffects = {
    EffectDefinition{"transform_effect", 98U, true, {}, 0},
    EffectDefinition{
        "zoom", 122U, false, {{{"scale", EffectOptionType::kInt, 2}}}, 1},
    EffectDefinition{"magic_lamp", 5U, true, {}, 0},
    EffectDefinition{"locate_pointer", 111U, false, {}, 0},
    EffectDefinition{"shake_view", 0U, true, {}, 0},
    EffectDefinition{"shake_cursor", 110U, true, {}, 0},
    EffectDefinition{"output_transform", 105U, true, {}, 0},
    EffectDefinition{"translation", 105U, true, {}, 0},
    EffectDefinition{"slide", 5U, true, {}, 0},
    EffectDefinition{"fade", 10U, true, {}, 0},
    EffectDefinition{"touch_trail", 100U, true, {}, 0},
    EffectDefinition{"touch_long", 102U, true, {}, 0},
    EffectDefinition{"touch_click", 100U, true, {}, 0},
    EffectDefinition{"mouse_trail", 100U, false, {}, 0},
    EffectDefinition{"mouse_click", 100U, false, {}, 0},
    EffectDefinition{"soft_gamma", 150U, true, {}, 0},
    EffectDefinition{"scale", 5U, true, {}, 0},
    EffectDefinition{
        "move", 0U, false, {{{"type", EffectOptionType::kInt, 0}}}, 1},
    EffectDefinition{"watermark", 0U, true, {}, 0},
    EffectDefinition{"blur",
                     0U,
                     true,
                     {{{"blur_strength", EffectOptionType::kInt, 4},
                       {"noise_strength", EffectOptionType::kInt, 0}}},
                     2},
    EffectDefinition{"showfps",
                     0U,
                     false,
                     {{{"always_repaint", EffectOptionType::kBool, 1}}},
                     1},
};

const EffectDefinition* FindEffect(const QString& name) {
  const auto found =
      std::find_if(kEffects.begin(), kEffects.end(),
                   [&](const auto& effect) { return name == effect.name; });
  return found == kEffects.end() ? nullptr : &*found;
}

// json-c keeps integers and doubles apart; QJsonValue does not, so declared
// options use their registry type and anything else is inferred.
std::optional<EffectOptionType> StoredOptionType(const EffectDefinition& effect,
                                                 const QString& key,
                                                 const QJsonValue& value) {
  if (key == QStringLiteral("enabled")) return EffectOptionType::kBool;
  for (size_t i = 0; i < effect.option_count; ++i) {
    if (key == effect.options[i].key) return effect.options[i].type;
  }
  if (value.isBool()) return EffectOptionType::kBool;
  if (value.isString()) return EffectOptionType::kString;
  if (value.isDouble()) {
    const double number = value.toDouble();
    return number == std::trunc(number) ? EffectOptionType::kInt
                                        : EffectOptionType::kDouble;
  }
  return std::nullopt;
}

std::optional<EffectOptionType> VariantOptionType(const QVariant& value) {
  switch (value.metaType().id()) {
    case QMetaType::Bool:
      return EffectOptionType::kBool;
    case QMetaType::Int:
      return EffectOptionType::kInt;
    case QMetaType::Double:
      return EffectOptionType::kDouble;
    case QMetaType::QString:
      return EffectOptionType::kString;
    default:
      return std::nullopt;
  }
}

// Mirrors each GXWM effect's configure() handler.
bool ConfigureEffect(const EffectDefinition& effect, const QString& key,
                     const QJsonValue& value, const QJsonObject& options) {
  const QString name = QString::fromLatin1(effect.name);
  if (name == QStringLiteral("shake_view")) return true;
  if (name == QStringLiteral("showfps")) {
    return key == QStringLiteral("always_repaint") &&
           value.toBool() != options.value(key).toBool();
  }
  if (key == QStringLiteral("enabled")) return true;
  if (name == QStringLiteral("blur")) {
    const int number = value.toInt();
    if (key == QStringLiteral("blur_strength")) {
      return number >= 1 && number <= 15;
    }
    if (key == QStringLiteral("noise_strength")) {
      return number >= 0 && number <= 14;
    }
    return false;
  }
  if (name == QStringLiteral("move")) {
    return key == QStringLiteral("type") &&
           (value.toInt() == 0 || value.toInt() == 1);
  }
  if (name == QStringLiteral("zoom")) {
    return key == QStringLiteral("scale") &&
           value.toInt() != options.value(key).toInt() &&
           value.toInt() <= kMaxZoomScale;
  }
  return false;
}

}  // namespace

QJsonObject WlcomDbusManager::EffectOptions(const QString& name) const {
  const EffectDefinition* effect = FindEffect(name);
  if (effect == nullptr) return {};
  QJsonObject options = system_config_.value(QStringLiteral("Effects"))
                            .toObject()
                            .value(name)
                            .toObject();
  const QJsonObject user = config_.value(QStringLiteral("Effects"))
                               .toObject()
                               .value(name)
                               .toObject();
  for (auto it = user.begin(); it != user.end(); ++it) {
    options[it.key()] = it.value();
  }
  if (!options.contains(QStringLiteral("enabled"))) {
    options[QStringLiteral("enabled")] = effect->enabled;
  }
  for (size_t i = 0; i < effect->option_count; ++i) {
    const EffectOptionDefault& option = effect->options[i];
    const QString key = QString::fromLatin1(option.key);
    if (options.contains(key)) continue;
    if (option.type == EffectOptionType::kBool) {
      options[key] = option.value != 0;
    } else {
      options[key] = option.value;
    }
  }
  return options;
}

void WlcomDbusManager::LoadEffectState() {
  effect_enabled_.clear();
  for (const auto& effect : kEffects) {
    const QString name = QString::fromLatin1(effect.name);
    effect_enabled_[name] =
        EffectOptions(name).value(QStringLiteral("enabled")).toBool();
  }
}

bool WlcomDbusManager::EffectEnabled(const QString& name) const {
  return effect_enabled_.value(name, false);
}

void WlcomDbusManager::ApplyBlurEffect() {
  if (compositor_ == nullptr || compositor_->protocol_manager_ == nullptr) {
    return;
  }
  const QJsonObject options = EffectOptions(QStringLiteral("blur"));
  compositor_->protocol_manager_->SetGlobalBlur(
      EffectEnabled(QStringLiteral("blur")),
      options.value(QStringLiteral("blur_strength")).toInt(4));
}

void WlcomDbusManager::ApplyEffectState(const QString& name) {
  if (name == QStringLiteral("blur")) {
    ApplyBlurEffect();
  } else if (name == QStringLiteral("shake_cursor") && compositor_ != nullptr &&
             compositor_->shake_cursor_ != nullptr) {
    compositor_->shake_cursor_->SetEnabled(EffectEnabled(name));
  }
}

// GXWM's MouseFinder SetEnabled: switch the shake_cursor effect, persist its
// enabled option and mirror it to UKUI's mouse settings.
void WlcomDbusManager::SetMouseFinderEnabled(bool enabled,
                                             bool mirror_to_gsettings) {
  const QString name = QStringLiteral("shake_cursor");
  effect_enabled_[name] = enabled;
  ApplyEffectState(name);
  QJsonObject options = EffectOptions(name);
  options[QStringLiteral("enabled")] = enabled;
  QJsonObject effects = config_.value(QStringLiteral("Effects")).toObject();
  effects[name] = options;
  config_[QStringLiteral("Effects")] = effects;
  SaveConfig();
#ifdef FLAKEWM_HAS_QGSETTINGS
  if (mirror_to_gsettings && mouse_settings_ != nullptr &&
      mouse_settings_->keys().contains(QStringLiteral("shakeCursor"))) {
    mouse_settings_->trySet(QStringLiteral("shakeCursor"), enabled);
  }
#else
  (void)mirror_to_gsettings;
#endif
}

void WlcomDbusManager::SetupMouseSettings() {
#ifdef FLAKEWM_HAS_QGSETTINGS
  constexpr char kSchema[] = "org.ukui.peripherals-mouse";
  if (!QGSettings::isSchemaInstalled(kSchema)) return;
  mouse_settings_ = std::make_unique<QGSettings>(kSchema);
  QObject::connect(
      mouse_settings_.get(), &QGSettings::changed, this,
      [this](const QString& key) {
        if (key != QStringLiteral("shakeCursor") &&
            key != QStringLiteral("shake-cursor")) {
          return;
        }
        const bool enabled =
            mouse_settings_->get(QStringLiteral("shakeCursor")).toBool();
        if (enabled != EffectEnabled(QStringLiteral("shake_cursor"))) {
          SetMouseFinderEnabled(enabled, false);
        }
      });
#endif
}

bool WlcomDbusManager::HandleMouseFinder(const QDBusMessage& message) {
  if (message.member() == QStringLiteral("GetEnabled")) {
    Reply(message, {EffectEnabled(QStringLiteral("shake_cursor"))});
    return true;
  }
  if (message.member() == QStringLiteral("SetEnabled")) {
    if (message.signature() != QStringLiteral("b")) {
      Error(message, kInvalidArgs, QStringLiteral("Expected boolean."));
      return true;
    }
    SetMouseFinderEnabled(message.arguments().value(0).toBool(), true);
    Reply(message, {true});
    return true;
  }
  return false;
}

bool WlcomDbusManager::HandleEffect(const QDBusMessage& message) {
  const QVariantList args = message.arguments();
  if (message.member() == QStringLiteral("ListAllEffects")) {
    QList<types::Effect> result;
    for (const auto& effect : kEffects) {
      const QString name = QString::fromLatin1(effect.name);
      result << types::Effect{name, effect.priority, EffectEnabled(name)};
    }
    Reply(message, {DbusArray(result)});
    return true;
  }

  const QString name = args.value(0).toString();
  const EffectDefinition* effect = FindEffect(name);
  if (message.member() == QStringLiteral("EnableEffect")) {
    if (effect == nullptr) {
      Error(message, kInvalidArgs, QStringLiteral("Invalid effect name."));
      return true;
    }
    effect_enabled_[name] = args.value(1).toBool();
    ApplyEffectState(name);
    Reply(message);
    return true;
  }
  if (message.member() == QStringLiteral("PrintEffectOptions")) {
    if (effect == nullptr) {
      Error(message, kInvalidArgs,
            QStringLiteral("Invalid effect name or no option."));
      return true;
    }
    Reply(message, {QString::fromUtf8(QJsonDocument(EffectOptions(name))
                                          .toJson(QJsonDocument::Compact))});
    return true;
  }
  if (message.member() == QStringLiteral("SetEffectOption")) {
    if (effect == nullptr) {
      Error(message, kInvalidArgs, QStringLiteral("Invalid effect name."));
      return true;
    }
    const QString key = args.value(1).toString();
    const QVariant value = UnwrapVariant(args.value(2));
    const std::optional<EffectOptionType> type = VariantOptionType(value);
    if (args.size() != 3 || !type.has_value()) {
      Error(message, kInvalidArgs, QStringLiteral("Invalid option type."));
      return true;
    }
    QJsonObject options = EffectOptions(name);
    if (!options.contains(key) ||
        StoredOptionType(*effect, key, options.value(key)) != type) {
      Error(message, kInvalidArgs,
            QStringLiteral("option not found or type error."));
      return true;
    }
    const QJsonValue json = QJsonValue::fromVariant(value);
    if (!ConfigureEffect(*effect, key, json, options)) {
      Error(message, kInvalidArgs,
            QStringLiteral("Effect rejected option value."));
      return true;
    }
    options[key] = json;
    QJsonObject effects = config_.value(QStringLiteral("Effects")).toObject();
    effects[name] = options;
    config_[QStringLiteral("Effects")] = effects;
    SaveConfig();
    if (name == QStringLiteral("blur")) ApplyBlurEffect();
    Reply(message);
    return true;
  }
  return false;
}

bool WlcomDbusManager::HandlePlugin(const QDBusMessage& message) {
  const QVariantList args = message.arguments();
  QJsonObject plugins = config_.value(QStringLiteral("plugins")).toObject();
  if (message.member() == QStringLiteral("ListAllPlugins")) {
    QList<types::Plugin> result;
    for (auto it = plugins.begin(); it != plugins.end(); ++it) {
      const QJsonObject value = it.value().toObject();
      result << types::Plugin{it.key(), value.value("loaded").toBool(),
                              value.value("enabled").toBool()};
    }
    Reply(message, {DbusArray(result)});
    return true;
  }
  const QString name = args.value(0).toString();
  if (!plugins.contains(name)) {
    Error(message, kInvalidArgs, "Invalid plugin name.");
    return true;
  }
  QJsonObject plugin = plugins.value(name).toObject();
  if (message.member() == QStringLiteral("PrintPluginInfo")) {
    if (!plugin.value("loaded").toBool()) {
      Error(message, kInvalidArgs, "Plugin is not loaded.");
      return true;
    }
    Reply(message,
          {plugin.value("vendor").toString(), plugin.value("class").toString(),
           plugin.value("description").toString(),
           static_cast<quint32>(plugin.value("version").toInt()),
           static_cast<quint32>(plugin.value("abi_version").toInt())});
    return true;
  }
  if (message.member() == QStringLiteral("PrintPluginConfig")) {
    Reply(message, {QString::fromUtf8(
                       QJsonDocument(plugin).toJson(QJsonDocument::Compact))});
    return true;
  }
  if (message.member() == QStringLiteral("LoadPlugin")) {
    plugin[QStringLiteral("loaded")] = args.value(1).toBool();
    plugins[name] = plugin;
    config_[QStringLiteral("plugins")] = plugins;
    SaveConfig();
    Reply(message);
    return true;
  }
  if (message.member() == QStringLiteral("EnablePlugin")) {
    if (!plugin.value("loaded").toBool()) {
      Error(message, kInvalidArgs, "Plugin is not loaded.");
      return true;
    }
    plugin[QStringLiteral("enabled")] = args.value(1).toBool();
    plugins[name] = plugin;
    config_[QStringLiteral("plugins")] = plugins;
    SaveConfig();
    Reply(message);
    return true;
  }
  if (message.member() == QStringLiteral("SetPluginOption")) {
    if (!plugin.value("loaded").toBool() || !plugin.value("enabled").toBool()) {
      Error(message, kInvalidArgs, "Plugin is not loaded or enabled.");
      return true;
    }
    QJsonObject options = plugin.value("options").toObject();
    if (!options.contains(args.value(1).toString())) {
      Error(message, kInvalidArgs, "option not found or type error.");
      return true;
    }
    options[args[1].toString()] =
        QJsonValue::fromVariant(UnwrapVariant(args.value(2)));
    plugin[QStringLiteral("options")] = options;
    plugins[name] = plugin;
    config_[QStringLiteral("plugins")] = plugins;
    SaveConfig();
    Reply(message);
    return true;
  }
  return false;
}

}  // namespace dbus
}  // namespace flakewm
