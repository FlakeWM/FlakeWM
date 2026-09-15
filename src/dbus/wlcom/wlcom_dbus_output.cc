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

bool WlcomDbusManager::HandleOutput(const QDBusMessage& message) {
  const QVariantList args = message.arguments();
  if (message.member() == QStringLiteral("ListAllOutputs")) {
    QList<types::StringPair> outputs;
    for (const auto& wrapper : compositor_->outputs_) {
      wlr_output* output = wrapper->handle;
      if (output == nullptr) continue;
      PersistOutput(output);
      const QJsonObject data = config_.value(QStringLiteral("outputs"))
                                   .toObject()
                                   .value(QString::fromUtf8(output->name))
                                   .toObject();
      outputs << types::StringPair{QString::fromUtf8(output->name),
                                   QString::fromUtf8(QJsonDocument(data).toJson(
                                       QJsonDocument::Compact))};
    }
    Reply(message, {DbusArray(outputs)});
    return true;
  }
  if (message.member() == QStringLiteral("SetBrightness") ||
      message.member() == QStringLiteral("SetColortemp")) {
    if (args.size() != 2 || FindOutput(args[0].toString()) == nullptr) {
      Error(message, kInvalidArgs, "Invalid output.");
      return true;
    }
    wlr_output* target = FindOutput(args[0].toString());
    QJsonObject outputs = config_.value(QStringLiteral("outputs")).toObject();
    QJsonObject values = outputs.value(args[0].toString()).toObject();
    const int brightness =
        message.member() == QStringLiteral("SetBrightness")
            ? static_cast<int>(std::min(args[1].toUInt(), 100U))
            : values.value(QStringLiteral("brightness")).toInt(100);
    const int color_temperature =
        message.member() == QStringLiteral("SetColortemp")
            ? static_cast<int>(std::clamp(args[1].toUInt(), 1000U, 25100U))
            : values.value(QStringLiteral("color_temp")).toInt(6500);
    if (!ApplyOutputColor(target, brightness, color_temperature)) {
      Reply(message);
      return true;
    }
    values[QStringLiteral("brightness")] = std::clamp(brightness, 0, 100);
    values[QStringLiteral("color_temp")] =
        std::clamp(color_temperature, 1000, 25100);
    outputs[args[0].toString()] = values;
    config_[QStringLiteral("outputs")] = outputs;
    PersistOutput(target);
    SaveConfig();
    Reply(message);
    return true;
  }
  if (message.member() == QStringLiteral("GetCursorOutput")) {
    wlr_output* output = wlr_output_layout_output_at(
        compositor_->output_layout_, compositor_->cursor_->x,
        compositor_->cursor_->y);
    Reply(message,
          {output == nullptr ? QString{} : QString::fromUtf8(output->name)});
    return true;
  }

  auto primary = [&]() -> wlr_output* {
    const QString saved = PrimaryOutputName();
    if (wlr_output* output = FindOutput(saved);
        output != nullptr && output->enabled)
      return output;
    for (const auto& item : compositor_->outputs_)
      if (item->handle != nullptr && item->handle->enabled) return item->handle;
    return nullptr;
  };
  auto fail = [&]() {
    Error(message, kInvalidArgs,
          "Invalid or unsupported output configuration.");
  };
  if (message.member() == QStringLiteral("SetScaleRatio")) {
    double scale = std::round(args.value(0).toDouble() * 100.0) / 100.0;
    if (args.size() != 1 || !std::isfinite(scale) || scale < 1.0 ||
        scale > 3.0 ||
        !ConfigureOutput(primary(), nullptr, &scale, nullptr, 0, 0, 0))
      fail();
    else {
      SaveConfig();
      Reply(message);
    }
    return true;
  }
  if (message.member() == QStringLiteral("SetResolutionWRefreshRate")) {
    const bool ok =
        args.size() == 3 && args[0].toInt() > 0 && args[1].toInt() > 0 &&
        args[2].toInt() > 0 &&
        ConfigureOutput(primary(), nullptr, nullptr, nullptr, args[0].toInt(),
                        args[1].toInt(), args[2].toInt() * 1000);
    if (!ok)
      fail();
    else {
      SaveConfig();
      Reply(message);
    }
    return true;
  }
  if (message.member() == QStringLiteral("SetScreenBrightness")) {
    wlr_output* output = FindOutput(args.value(0).toString());
    if (args.size() != 2 || args[1].toInt() < 0 || args[1].toInt() > 100 ||
        output == nullptr)
      fail();
    else {
      QJsonObject outputs = config_.value(QStringLiteral("outputs")).toObject();
      QJsonObject state = outputs.value(args[0].toString()).toObject();
      const int temperature =
          state.value(QStringLiteral("color_temp")).toInt(6500);
      if (!ApplyOutputColor(output, args[1].toInt(), temperature)) {
        Error(message, kFailed,
              QStringLiteral(
                  "The screen does not support the requested brightness."));
        return true;
      }
      state[QStringLiteral("brightness")] = args[1].toInt();
      outputs[args[0].toString()] = state;
      config_[QStringLiteral("outputs")] = outputs;
      PersistOutput(output);
      SaveConfig();
      Reply(message);
    }
    return true;
  }
  if (message.member() == QStringLiteral("RotateScreen")) {
    const int degrees = args.value(0).toInt();
    wl_output_transform transform = TransformForDegrees(degrees);
    if (!ValidDegrees(degrees) ||
        !ConfigureOutput(primary(), nullptr, nullptr, &transform, 0, 0, 0))
      fail();
    else {
      SaveConfig();
      Reply(message);
    }
    return true;
  }
  if (message.member() == QStringLiteral("SetScreenScale")) {
    double scale = std::round(args.value(1).toDouble() * 100.0) / 100.0;
    if (args.size() != 2 || !std::isfinite(scale) || scale < 1.0 ||
        scale > 3.0 ||
        !ConfigureOutput(FindOutput(args[0].toString()), nullptr, &scale,
                         nullptr, 0, 0, 0))
      fail();
    else {
      SaveConfig();
      Reply(message);
    }
    return true;
  }
  if (message.member() == QStringLiteral("SetScreenResolution")) {
    const bool ok = args.size() == 4 && args[1].toInt() > 0 &&
                    args[2].toInt() > 0 && args[3].toInt() > 0 &&
                    ConfigureOutput(FindOutput(args[0].toString()), nullptr,
                                    nullptr, nullptr, args[1].toInt(),
                                    args[2].toInt(), args[3].toInt() * 1000);
    if (!ok)
      fail();
    else {
      SaveConfig();
      Reply(message);
    }
    return true;
  }
  if (message.member() == QStringLiteral("SetScreenRotation")) {
    const int degrees = args.value(1).toInt();
    wl_output_transform transform = TransformForDegrees(degrees);
    if (args.size() != 2 || !ValidDegrees(degrees) ||
        !ConfigureOutput(FindOutput(args[0].toString()), nullptr, nullptr,
                         &transform, 0, 0, 0))
      fail();
    else {
      SaveConfig();
      Reply(message);
    }
    return true;
  }
  if (message.member() == QStringLiteral("SetScreenEnabled")) {
    wlr_output* output = FindOutput(args.value(0).toString());
    bool enabled = args.value(1).toBool();
    int enabled_count = 0;
    for (const auto& item : compositor_->outputs_)
      enabled_count += item->handle != nullptr && item->handle->enabled;
    if (args.size() != 2 || output == nullptr ||
        (!enabled && output->enabled && enabled_count <= 1) ||
        !ConfigureOutput(output, &enabled, nullptr, nullptr, 0, 0, 0))
      fail();
    else {
      SaveConfig();
      Reply(message);
    }
    return true;
  }
  if (message.member() == QStringLiteral("SetPrimaryScreen")) {
    wlr_output* output = FindOutput(args.value(0).toString());
    if (args.size() != 1 || output == nullptr || !output->enabled)
      fail();
    else {
      SetPrimaryOutputName(args[0].toString());
      for (const auto& item : compositor_->outputs_)
        if (item->handle != nullptr) PersistOutput(item->handle);
      SaveConfig();
      Reply(message);
    }
    return true;
  }
  if (message.member() == QStringLiteral("SetScreenPosition")) {
    if (args.size() != 3 || !MoveOutput(FindOutput(args[0].toString()),
                                        args[1].toInt(), args[2].toInt()))
      fail();
    else {
      SaveConfig();
      Reply(message);
    }
    return true;
  }
  if (message.member() == QStringLiteral("SetScreenMode")) {
    const quint32 mode = args.value(0).toUInt();
    const QString only = args.value(1).toString();
    if (args.size() != 2 || mode > 2 ||
        (mode == 2 && (only.isEmpty() || FindOutput(only) == nullptr))) {
      fail();
      return true;
    }
    int duplicate_width = 0;
    int duplicate_height = 0;
    if (mode == 0) {
      std::vector<wlr_output*> candidates;
      for (const auto& item : compositor_->outputs_)
        if (item->handle != nullptr) candidates.push_back(item->handle);
      if (candidates.size() < 2 || wl_list_empty(&candidates.front()->modes)) {
        fail();
        return true;
      }
      wlr_output_mode* candidate = nullptr;
      wl_list_for_each(candidate, &candidates.front()->modes, link) {
        const long long area =
            static_cast<long long>(candidate->width) * candidate->height;
        if (area <= static_cast<long long>(duplicate_width) * duplicate_height)
          continue;
        bool supported = true;
        for (wlr_output* output : candidates) {
          bool found = false;
          wlr_output_mode* output_mode = nullptr;
          wl_list_for_each(output_mode, &output->modes, link) {
            if (output_mode->width == candidate->width &&
                output_mode->height == candidate->height) {
              found = true;
              break;
            }
          }
          if (!found) {
            supported = false;
            break;
          }
        }
        if (supported) {
          duplicate_width = candidate->width;
          duplicate_height = candidate->height;
        }
      }
      if (duplicate_width == 0 || duplicate_height == 0) {
        fail();
        return true;
      }
    }
    int x = 0;
    for (const auto& item : compositor_->outputs_) {
      wlr_output* output = item->handle;
      if (output == nullptr) continue;
      bool enabled = mode != 2 || QString::fromUtf8(output->name) == only;
      const int width = mode == 0 ? duplicate_width : 0;
      const int height = mode == 0 ? duplicate_height : 0;
      if (!ConfigureOutput(output, &enabled, nullptr, nullptr, width, height,
                           0)) {
        fail();
        return true;
      }
      if (enabled && !MoveOutput(output, mode == 0 ? 0 : x, 0)) {
        fail();
        return true;
      }
      if (enabled && mode == 1)
        x += static_cast<int>(std::ceil(output->width / output->scale));
    }
    if (mode == 2) SetPrimaryOutputName(only);
    SaveConfig();
    Reply(message);
    return true;
  }
  if (message.member() == QStringLiteral("SetScreenLayout")) {
    if (args.size() != 1 ||
        args[0].metaType() != QMetaType::fromType<QDBusArgument>()) {
      fail();
      return true;
    }
    const QDBusArgument array = args[0].value<QDBusArgument>();
    QHash<QString, QPoint> positions;
    array.beginArray();
    while (!array.atEnd()) {
      QString name;
      qint32 x = 0, y = 0;
      array.beginStructure();
      array >> name >> x >> y;
      array.endStructure();
      if (positions.contains(name)) {
        array.endArray();
        fail();
        return true;
      }
      positions.insert(name, QPoint(x, y));
    }
    array.endArray();
    for (const auto& item : compositor_->outputs_) {
      if (item->handle == nullptr || !item->handle->enabled) continue;
      const QString name = QString::fromUtf8(item->handle->name);
      if (!positions.contains(name)) {
        fail();
        return true;
      }
    }
    for (auto it = positions.cbegin(); it != positions.cend(); ++it) {
      wlr_output* output = FindOutput(it.key());
      if (output == nullptr || !output->enabled ||
          !MoveOutput(output, it->x(), it->y())) {
        fail();
        return true;
      }
    }
    SaveConfig();
    Reply(message);
    return true;
  }
  return false;
}

}  // namespace dbus
}  // namespace flakewm
