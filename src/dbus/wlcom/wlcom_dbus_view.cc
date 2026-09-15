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

void WlcomDbusManager::SetShowDesktop(bool enabled) {
  if (show_desktop_ == enabled) return;
  show_desktop_ = enabled;
  if (enabled) {
    show_desktop_windows_.clear();
    for (const auto& window : compositor_->toplevels_) {
      if (window->mapped && !window->minimized && window->CanMinimize()) {
        show_desktop_windows_.insert(reinterpret_cast<quintptr>(window.get()));
        compositor_->Minimize(window.get());
      }
    }
  } else {
    for (const auto& window : compositor_->toplevels_) {
      if (show_desktop_windows_.contains(
              reinterpret_cast<quintptr>(window.get())) &&
          window->mapped && window->IsAlive())
        window->SetMinimizedState(false);
    }
    show_desktop_windows_.clear();
    compositor_->FocusNextToplevel(nullptr);
  }
}

bool WlcomDbusManager::HandleView(const QDBusMessage& message) {
  const QVariantList args = message.arguments();
  QJsonObject views = config_.value(QStringLiteral("Views")).toObject();
  auto persist_views = [&]() {
    config_[QStringLiteral("Views")] = views;
    SaveConfig();
  };
  if (message.member() == QStringLiteral("GetViewAdsorption")) {
    Reply(message, {views.value(QStringLiteral("view_adsorption")).toInt(3)});
    return true;
  }
  if (message.member() == QStringLiteral("SetViewAdsorption")) {
    const quint32 value = args.value(0).toUInt();
    if (args.size() != 1 || value > 3)
      Error(message, kInvalidArgs, "Invalid adsorption.");
    else {
      views[QStringLiteral("view_adsorption")] = static_cast<int>(value);
      persist_views();
      Reply(message);
    }
    return true;
  }
  if (message.member() == QStringLiteral("SetCSDRoundCorner")) {
    views[QStringLiteral("csd_round_corner")] = args.value(0).toBool();
    persist_views();
    Reply(message);
    return true;
  }
  if (message.member() == QStringLiteral("GetMinimizeEffect")) {
    Reply(message, {views.value(QStringLiteral("minimize_effect")).toInt(0)});
    return true;
  }
  if (message.member() == QStringLiteral("SetMinimizeEffect")) {
    const quint32 value = args.value(0).toUInt();
    if (args.size() != 1 || value >= 4)
      Error(message, kInvalidArgs, "Invalid minimize effect.");
    else {
      views[QStringLiteral("minimize_effect")] = static_cast<int>(value);
      persist_views();
      Reply(message);
    }
    return true;
  }
  if (message.member() == QStringLiteral("GetIsShowDesktop")) {
    Reply(message, {show_desktop_});
    return true;
  }
  if (message.member() == QStringLiteral("SetShowDesktop")) {
    SetShowDesktop(args.value(0).toBool());
    Reply(message);
    return true;
  }
  if (message.member() == QStringLiteral("ToggleShowDesktop")) {
    SetShowDesktop(!show_desktop_);
    Reply(message);
    return true;
  }
  if (message.member() == QStringLiteral("ListAllViews")) {
    QList<types::View> views;
    for (const auto& window : compositor_->toplevels_) {
      if (!window->mapped || !window->IsAlive()) continue;
      views << types::View{
          QString::fromUtf8(window->AppId()),
          QString::number(reinterpret_cast<quintptr>(window.get()), 16),
          SurfacePid(window->Surface())};
    }
    Reply(message, {DbusArray(views)});
    return true;
  }
  if (message.member() == QStringLiteral("ListViewStates")) {
    bool ok = false;
    const quintptr id = args.value(0).toString().toULongLong(&ok, 16);
    core::CompositorPrivate::Toplevel* found = nullptr;
    for (const auto& window : compositor_->toplevels_)
      if (reinterpret_cast<quintptr>(window.get()) == id) found = window.get();
    if (!ok || found == nullptr) {
      Error(message, kInvalidArgs, "Invalid uuid.");
      return true;
    }
    const wlr_box geometry = found->FrameGeometry();
    QList<types::State> states{
        {QStringLiteral("role"), {0}},
        {QStringLiteral("geometry"),
         {geometry.x, geometry.y, geometry.width, geometry.height}},
        {QStringLiteral("margin"), {0, 0, 0, 0}},
        {QStringLiteral("padding"), {0, 0, 0, 0}},
        {QStringLiteral("ssd"), {found->ssd != nullptr}},
        {QStringLiteral("minimize size"), {0, 0}},
        {QStringLiteral("maximize size"), {0, 0}},
        {QStringLiteral("kept_above"), {0}},
        {QStringLiteral("kept_below"), {0}},
        {QStringLiteral("minimized"), {found->minimized}},
        {QStringLiteral("maximized"), {found->maximized}},
        {QStringLiteral("fullscreen"), {found->RequestedFullscreen()}},
        {QStringLiteral("activated"),
         {compositor_->ToplevelForSurface(
              compositor_->seat_->keyboard_state.focused_surface) == found}},
        {QStringLiteral("tiled"), {0}},
        {QStringLiteral("modal"), {0}},
        {QStringLiteral("skip_taskbar"), {0}},
        {QStringLiteral("skip_switcher"), {0}}};
    Reply(message, {DbusArray(states)});
    return true;
  }
  if (message.member() == QStringLiteral("ListAllModes")) {
    Reply(message, {DbusArray(QList<types::NamedBool>{
                       {QStringLiteral("stack_mode"), true}})});
    return true;
  }
  return false;
}

bool WlcomDbusManager::HandleDeepinWm(const QDBusMessage& message) {
  if (message.member() == QStringLiteral("PerformAction")) {
    if (message.arguments().size() != 1) {
      Error(message, kInvalidArgs,
            QStringLiteral("PerformAction expects one int32."));
      return true;
    }
    const int action = message.arguments()[0].toInt();
    if (action == 1 || action == 2) {
      QDBusMessage call = QDBusMessage::createMethodCall(
          QStringLiteral("org.kylin.switch"), QStringLiteral("/MultitaskView"),
          QStringLiteral("org.kylin.switch.MultitaskView"),
          action == 1 ? QStringLiteral("showMultitaskView")
                      : QStringLiteral("show"));
      session_bus_.send(call);
    } else {
      Error(message, QStringLiteral("org.freedesktop.DBus.Error.NotSupported"),
            QStringLiteral("Action %1 is not supported").arg(action));
      return true;
    }
    Reply(message);
    return true;
  }
  if (message.member() == QStringLiteral("GetIsShowDesktop")) {
    Reply(message, {show_desktop_});
    return true;
  }
  if (message.member() == QStringLiteral("SetShowDesktop")) {
    SetShowDesktop(message.arguments().value(0).toBool());
    Reply(message);
    return true;
  }
  return false;
}

bool WlcomDbusManager::HandlePresentWindows(const QDBusMessage& message) {
  if (message.member() != QStringLiteral("presentWindows")) return false;
  if (message.arguments().size() != 1) {
    Error(message, kInvalidArgs,
          QStringLiteral("presentWindows expects a string array."));
    return true;
  }

  QDBusMessage call = QDBusMessage::createMethodCall(
      QStringLiteral("org.kylin.switch"), QStringLiteral("/MultitaskView"),
      QStringLiteral("org.kylin.switch.MultitaskView"), QStringLiteral("show"));
  session_bus_.send(call);
  Reply(message);
  return true;
}

}  // namespace dbus
}  // namespace flakewm
