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
 */

#include <qwlogging.h>
#include <woutputrenderwindow.h>
#include <wrenderhelper.h>

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <WServer>

#include "src/core/compositor/compositor.h"

WAYLIB_SERVER_USE_NAMESPACE

int main(int argc, char* argv[]) {
  // We referenced waylib/examples/tinywl/main.cpp.
  // First of all, we need to initialize wlroots logging & QtQuick render
  // backend, then we let Waylib install its own QPA. The order does matter.
  qw_log::init();
  WRenderHelper::setupRendererBackend();
  WServer::initializeQPA();

  // Also referenced Waylib version of TinyWL.
  // Curently Waylib render path uses OpenGL ES, and we want to keep
  // non-integer scale factor. The compositor should NOT exit the whole desktop
  // session just because a top-level Qt window is closed.
  QGuiApplication::setAttribute(Qt::AA_UseOpenGLES);
  QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
    Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
  QGuiApplication::setQuitOnLastWindowClosed(false);

  QGuiApplication application(argc, argv);
  QGuiApplication::setApplicationName(QStringLiteral("FlakeWM"));
  QGuiApplication::setDesktopFileName(QStringLiteral("flakewm"));
  QGuiApplication::setOrganizationName(QStringLiteral("FlakeWM"));

  QQmlApplicationEngine engine;

#ifdef FLAKEWM_WAYLIB_QML_IMPORT_PATH
  // Only when building from vendored Waylib: QML pathes are injected via
  // CMake.
  engine.addImportPath(QStringLiteral(FLAKEWM_WAYLIB_QML_IMPORT_PATH));
#endif
  QObject::connect(
    &engine, &QQmlApplicationEngine::objectCreationFailed, &application,
    [] { QCoreApplication::exit(EXIT_FAILURE); }, Qt::QueuedConnection);
  engine.loadFromModule("FlakeWM", "Main");

  if (engine.rootObjects().isEmpty()) {
    return EXIT_FAILURE;
  }

  // Create a QML scene first and then we kickstart compositor.
  // Then, start() can bind the existing OutputRenderWindow tp wlroots
  // renderer/allocator.
  // Compositor is a C++ singleton that is registered by qt_add_qml_module.
  auto* window = engine.rootObjects().constFirst()
    ->findChild<WOutputRenderWindow*>();
  auto* compositor = engine.singletonInstance<FlakeCompositor*>(
    "FlakeWM", "Compositor");
  if (!window || !compositor || !compositor->Start(window, &engine)) {
    qCritical("(Compositor) Init: Failed to initialize compositor, halted!!");
    return EXIT_FAILURE;
  }

  return application.exec();
}
