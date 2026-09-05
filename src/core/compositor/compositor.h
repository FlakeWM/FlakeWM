/*
 * Copyright (C) 2026 CharOfString <root@charofstring.cc>
 *
 * This file is part of FlakeWM.
 *
 * FlakeWM is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * FlakeWM is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * FlakeWM. If not, see <https://www.gnu.org/licenses/>.
 * ----------------------------------------------------------------------------
 * Referenced:
 *   - waylib/examples/outputviewport/helper.{h,cpp}: Output, render, viewport
 *     lifecycle.
 *   - waylib/examples/tinywl/helper.{h,cpp}: The start sequence of seat,
 *     xdg-shell, socket and backend.
 *   - Treeland's src/core/treeland.{h,cpp}: The separation of compositor
 *     lifecycle in C++ and scene in QML.
 * ----------------------------------------------------------------------------
 * The core of compositor. Currently we have a minimal set of xdg-shell policy,
 * WQmlCreator model and horizontal output layout.
 * This is still an EARLY STAGED version.
 * ----------------------------------------------------------------------------
 * The class FlakeWM bridges the Wayland server and QML scene.
 * It holds core objects of Wayland display, and transfers output,
 * xdg_toplevel and xdg_popup to WQmlCreator.
 * QML will instantiate corresponding Waylib Item for each object, so that
 * the protocol object lifecycle will align w/ the scene graph.
 */

#pragma once

#include <wglobal.h>
#include <wqmlcreator.h>

#include <QObject>
#include <QQmlEngine>

Q_MOC_INCLUDE(<wcursor.h>)
Q_MOC_INCLUDE(<wquickoutputlayout.h>)

WAYLIB_SERVER_BEGIN_NAMESPACE
class WBackend;
class WCursor;
class WOutputRenderWindow;
class WQuickOutputLayout;
class WSeat;
class WServer;
class WSocket;
class WXdgShell;
WAYLIB_SERVER_END_NAMESPACE

QW_BEGIN_NAMESPACE
class qw_allocator;
class qw_compositor;
class qw_renderer;
QW_END_NAMESPACE

WAYLIB_SERVER_USE_NAMESPACE
QW_USE_NAMESPACE

class FlakeCompositor final : public QObject {
  Q_OBJECT
  QML_NAMED_ELEMENT(Compositor)
  QML_SINGLETON

  // Got 3 creators: dynamic outputs, normal windows and popups.
  Q_PROPERTY(WQmlCreator* outputs READ Outputs CONSTANT FINAL)
  Q_PROPERTY(WQmlCreator* toplevels READ Toplevels CONSTANT FINAL)
  Q_PROPERTY(WQmlCreator* popups READ Popups CONSTANT FINAL)

  // outputLayout is a global coordinate system shared by all outputs.
  // cursor uses the same coordinate system.
  Q_PROPERTY(WQuickOutputLayout* outputLayout READ OutputLayout CONSTANT FINAL)
  Q_PROPERTY(WCursor* cursor READ Cursor CONSTANT FINAL)

  // socketName only have value after a successful autoCreate()
  // This is for launcher/debugging tool to connect to the client.
  Q_PROPERTY(QString socketName READ SocketName NOTIFY socketNameChanged FINAL)

 public:
  explicit FlakeCompositor(QObject* parent = nullptr);

  WQmlCreator* Outputs() const;
  WQmlCreator* Toplevels() const;
  WQmlCreator* Popups() const;
  WQuickOutputLayout* OutputLayout() const;
  WCursor* Cursor() const;
  QString SocketName() const;

  // Note that the connection of signals, initialization of render/allocator,
  // creation of socket and boot of backend MUST be done BEFORE Main.qml is
  // created, and AFTER WOutputRenderWindow is created. Each instance shall
  // only call this function once.
  bool Start(WOutputRenderWindow* window, QQmlEngine* engine);

 Q_SIGNALS:
  void socketNameChanged();

 private:
  // Wayland server core and input/output backend.
  // QObject parent is responsible for the Qt side destruction order.
  WServer* server_ = nullptr;
  WBackend* backend_ = nullptr;
  WSeat* seat_ = nullptr;
  WCursor* cursor_ = nullptr;
  WQuickOutputLayout* output_layout_ = nullptr;

  // Convert C++ protocol objects to QML delegate instances.
  WQmlCreator* outputs_ = nullptr;
  WQmlCreator* toplevels_ = nullptr;
  WQmlCreator* popups_ = nullptr;

  // Current minimal protocol set:
  // xdg-shell client, and an auto-named Wayland socket.
  WXdgShell* xdg_shell_ = nullptr;
  WSocket* socket_ = nullptr;

  // wlroots's native render objects are used by Waylib's OutputRenderWindow.
  qw_renderer* renderer_ = nullptr;
  qw_allocator* allocator_ = nullptr;
  qw_compositor* compositor_ = nullptr;

  // A flag that records whether Start() has been called so that we can ensure
  // it is only called once.
  bool started_ = false;
};
