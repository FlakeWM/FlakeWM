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

#include <qwallocator.h>
#include <qwbackend.h>
#include <qwcompositor.h>
#include <qwdisplay.h>
#include <qwoutput.h>
#include <qwrenderer.h>
#include <qwsubcompositor.h>
#include <wcursor.h>
#include <woutputrenderwindow.h>
#include <woutputviewport.h>
#include <wquickoutputlayout.h>
#include <wrenderhelper.h>
#include <wsocket.h>
#include <wxdgpopupsurface.h>
#include <wxdgshell.h>
#include <wxdgtoplevelsurface.h>

#include <QDebug>
#include <QJSValue>
#include <WBackend>
#include <WOutput>
#include <WSeat>
#include <WServer>

#include "src/core/compositor/compositor.h"

FlakeCompositor::FlakeCompositor(QObject* parent) : QObject(parent),
    server_(new WServer(this)),
    cursor_(new WCursor(this)),
    output_layout_(new WQuickOutputLayout(server_)),
    outputs_(new WQmlCreator(this)),
    toplevels_(new WQmlCreator(this)),
    popups_(new WQmlCreator(this)) {
  // Referred waylib/examples/tinywl/helper.cpp's Helper::init()
  // Both seat and backend are mounted as WServer interface, and this must be
  // done before WServer::start().
  seat_ = server_->attach<WSeat>();
  seat_->setCursor(cursor_);

  // cursor_ shares WQuickOutputLayout w/ OutputItem.
  // The global coordinate of cursor_ can be correctly converted to local
  // coordinates.
  // The layut object is parented to server_, so it will be destroyed later
  // than outputs.
  cursor_->setLayout(output_layout_);

  backend_ = server_->attach<WBackend>();

  // 5 is the xdg_wm_base that published to clients, following the current
  // Waylib TinyWL example.
  // TODO: This is only a minimal implementation, advanced features, such as
  //       layer-shell, shall be done later.
  xdg_shell_ = server_->attach<WXdgShell>(5);
}

WQmlCreator* FlakeCompositor::Outputs() const {
  return outputs_;
}

WQmlCreator* FlakeCompositor::Toplevels() const {
  return toplevels_;
}

WQmlCreator* FlakeCompositor::Popups() const {
  return popups_;
}

WQuickOutputLayout* FlakeCompositor::OutputLayout() const {
  return output_layout_;
}

WCursor* FlakeCompositor::Cursor() const {
  return cursor_;
}

QString FlakeCompositor::SocketName() const {
  return socket_ ? socket_->fullServerName() : QString();
}

bool FlakeCompositor::Start(WOutputRenderWindow* window, QQmlEngine* engine) {
  // Start() creates non-repeatable Wayland globals and a socket, so a failed
  // call must not be blindly retried on the same instance. The upper layer
  // should exit and reconstruct the entire compositor.
  if (started_ || !window || !engine) {
    return false;
  }
  started_ = true;

  // The output/input signal wiring references
  // outputviewport/helper.cpp::initProtocols() and tinywl/helper.cpp::init().
  // Unlike the examples, FlakeWM hands objects to QML via WQmlCreator instead
  // of directly `new OutputItem` in C++.
  connect(backend_, &WBackend::outputAdded, this,
      [this, engine](WOutput* output) {
    // Nested Wayland/X11 backends typically lack a DRM hardware cursor
    // plane. Forcing a software cursor ensures the cursor still appears
    // in the composited result under development environments.
    if (!backend_->hasDrm()) {
      output->setForceSoftwareCursor(true);
    }

    // waylandOutput maps to the required property in Main.qml.
    // x is set to the current layout width, implementing FlakeWM's
    // temporary "new outputs stack left to right" strategy. This is
    // not derived from Treeland or TinyWL.
    QJSValue properties = engine->newObject();
    properties.setProperty(QStringLiteral("waylandOutput"),
      engine->toScriptValue(output));
    properties.setProperty(QStringLiteral("x"),
      output_layout_->implicitWidth());
      outputs_->add(output, properties);
  });

  connect(backend_, &WBackend::outputRemoved, this,
      [this](WOutput* output) {
    outputs_->removeByOwner(output);
  });

  connect(backend_, &WBackend::inputAdded, this,
      [this](WInputDevice* device) {
    seat_->attachInputDevice(device);
  });

  connect(backend_, &WBackend::inputRemoved, this,
      [this](WInputDevice* device) {
    seat_->detachInputDevice(device);
  });

  // xdg-shell routing references tinywl/helper.cpp.
  // Once the owner is given to WQmlCreator, removeByOwner synchronously
  // removes the QML delegate when the protocol object is destroyed, avoiding
  // dangling shellSurface references.
  connect(xdg_shell_, &WXdgShell::toplevelSurfaceAdded, this,
      [this, engine](WXdgToplevelSurface* surface) {
    QJSValue properties = engine->newObject();
    properties.setProperty(QStringLiteral("waylandSurface"),
      engine->toScriptValue(surface));
    toplevels_->add(surface, properties);
  });

  connect(xdg_shell_, &WXdgShell::toplevelSurfaceRemoved, this,
      [this](WXdgToplevelSurface* surface) {
    toplevels_->removeByOwner(surface);
  });

  connect(xdg_shell_, &WXdgShell::popupSurfaceAdded, this,
      [this, engine](WXdgPopupSurface* surface) {
    QJSValue properties = engine->newObject();
    properties.setProperty(QStringLiteral("waylandSurface"),
      engine->toScriptValue(surface));
    popups_->add(surface, properties);
  });

  connect(xdg_shell_, &WXdgShell::popupSurfaceRemoved, this,
      [this](WXdgPopupSurface* surface) {
    popups_->removeByOwner(surface);
  });

  // WServer::start() first creates the wl_display and globals for all attached
  // interfaces. The startup order of renderer, socket and backend follows the
  // outputviewport and TinyWL examples.
  server_->start();

  // WRenderHelper selects a renderer based on WLR_RENDERER and backend
  // capabilities. The allocator must use the same backend/renderer pair,
  // otherwise client buffers may fail to import.
  renderer_ = WRenderHelper::createRenderer(backend_->handle());
  if (!renderer_) {
    qCritical() << "(Compositor) Init: Failed to create a renderer!";
    return false;
  }

  allocator_ = qw_allocator::autocreate(*backend_->handle(), *renderer_);
  if (!allocator_) {
    qCritical() << "(Compositor) Init: Failed to create a buffer allocator!";
    return false;
  }

  // Register the renderer's shm/dmabuf capabilities with wl_display, then
  // publish wl_compositor v6 and wl_subcompositor. This initialisation
  // sequence is taken directly from outputviewport/helper.cpp.
  renderer_->init_wl_display(*server_->handle());
  compositor_ = qw_compositor::create(*server_->handle(), 6, *renderer_);
  qw_subcompositor::create(*server_->handle());

  // Reference outputviewport/helper.cpp: wait until OutputViewport has
  // created its swapchain and render target before enabling the physical
  // output. This avoids the backend sending frames while the Qt scene has no
  // render target yet.
  connect(window, &WOutputRenderWindow::outputViewportInitialized, this,
      [](WOutputViewport* viewport) {
    WOutput* output = viewport->output();
    qw_output* handle = output->handle();
    if (handle->property("_flakewmEnabled").toBool()) {
      return;
    }

    qw_output_state state;

    // Nested outputs often already have a current_mode; real DRM
    // outputs prefer the preferred mode on first enable.
    // _flakewmEnabled is a local idempotency guard to avoid repeated
    // commits.
    if (!handle->handle()->current_mode) {
      if (auto* mode = handle->preferred_mode()) {
        state.set_mode(mode);
      }
    }

    state.set_enabled(true);
    if (handle->commit_state(state)) {
      handle->setProperty("_flakewmEnabled", true);
    } else {
      qCritical() << "(Compositor) Init: Failed to enable output" << output;
    }
  });

  window->init(renderer_, allocator_);

  // Reference tinywl/helper.cpp: `false` means we do not take over the socket
  // as an external fd. autoCreate() picks an available wayland-N under
  // XDG_RUNTIME_DIR, then hands it to WServer to listen on the event loop.
  socket_ = new WSocket(false, nullptr, server_);
  if (!socket_->autoCreate()) {
    qCritical() << "(Compositor) Init: Failed to create a Wayland socket!!";
    return false;
  }
  server_->addSocket(socket_);
  Q_EMIT socketNameChanged();

  // The backend is started last. Once started it immediately enumerates
  // outputs/inputs and emits the signals wired above, so it must not be
  // brought forward before creator, renderer, window or socket initialisation.
  if (!backend_->handle()->start()) {
    qCritical() << "(Compositor) Init: Failed to start the wlroots backend!!";
    return false;
  }

  qInfo().noquote()
    << "(Compositor) Init: FlakeWM is now listening on"
    << SocketName();

  return true;
}
