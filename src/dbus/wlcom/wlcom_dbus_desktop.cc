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
    const bool ok = valid && SetAvailableGtkSettings(
                                 {{QStringLiteral("org.gnome.desktop.interface"),
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
    const bool ok = GtkThemeInstalled(name) &&
                    SetAvailableGtkSettings(
                        {{QStringLiteral("org.ukui.style"),
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

bool WlcomDbusManager::HandleEffect(const QDBusMessage& message) {
  struct EffectDefinition {
    const char* name;
    quint32 priority;
    bool enabled;
  };

  // Source effects from GXWM
  static constexpr std::array effects = {
      EffectDefinition{"transform_effect", 98U, true},
      EffectDefinition{"zoom", 122U, false},
      EffectDefinition{"locate_pointer", 111U, false},
      EffectDefinition{"shake_view", 0U, true},
      EffectDefinition{"shake_cursor", 110U, false},
      EffectDefinition{"translation", 105U, true},
      EffectDefinition{"slide", 5U, true},
      EffectDefinition{"fade", 10U, true},
      EffectDefinition{"scale", 5U, true},
      EffectDefinition{"move", 0U, false},
      EffectDefinition{"watermark", 0U, true},
      EffectDefinition{"showfps", 0U, false}};
  QJsonObject values = config_.value(QStringLiteral("Effects")).toObject();
  auto known = [](const QString& name) {
    return std::any_of(effects.begin(), effects.end(),
                       [&](const auto& item) { return name == item.name; });
  };
  if (message.member() == QStringLiteral("ListAllEffects")) {
    QList<types::Effect> result;
    for (const auto& effect : effects) {
      const QJsonObject item =
          values.value(QString::fromLatin1(effect.name)).toObject();
      result << types::Effect{
          QString::fromLatin1(effect.name), effect.priority,
          item.value(QStringLiteral("enabled")).toBool(effect.enabled)};
    }
    Reply(message, {DbusArray(result)});
    return true;
  }
  const QVariantList args = message.arguments();
  const QString name = args.value(0).toString();
  if (!known(name)) {
    Error(message, kInvalidArgs, "Invalid effect name.");
    return true;
  }
  QJsonObject item = values.value(name).toObject();
  if (message.member() == QStringLiteral("EnableEffect")) {
    item[QStringLiteral("enabled")] = args.value(1).toBool();
    values[name] = item;
    config_[QStringLiteral("Effects")] = values;
    SaveConfig();
    Reply(message);
    return true;
  }
  if (message.member() == QStringLiteral("PrintEffectOptions")) {
    if (!item.contains(QStringLiteral("enabled"))) {
      const auto definition =
          std::find_if(effects.begin(), effects.end(),
                       [&](const auto& effect) { return name == effect.name; });
      item[QStringLiteral("enabled")] = definition->enabled;
    }
    Reply(message, {QString::fromUtf8(
                       QJsonDocument(item).toJson(QJsonDocument::Compact))});
    return true;
  }
  if (message.member() == QStringLiteral("SetEffectOption")) {
    if (args.size() != 3) {
      Error(message, kInvalidArgs, "Invalid option.");
      return true;
    }
    const QString option = args[1].toString();
    const QVariant value = UnwrapVariant(args[2]);
    if (!item.contains(option) || option == QStringLiteral("enabled")) {
      Error(message, kInvalidArgs, "option not found or type error.");
      return true;
    }
    item[option] = QJsonValue::fromVariant(value);
    values[name] = item;
    config_[QStringLiteral("Effects")] = values;
    SaveConfig();
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
