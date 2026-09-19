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
 * Adapted from GXDE-Wlcom, originally licensed under GPLv3.
 * Code has been modified to fit in Wlroots 0.20.2 & C++.
 * Now re-licensed under GPLv3.
 */

#include "src/view/ssd/window_menu/window_menu_renderer.h"

#include <absl/log/absl_log.h>

#include <QCoreApplication>
#include <QGuiApplication>
#include <QPalette>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QUrl>

#include "src/view/ssd/ssd_buffer/ssd_buffer.h"

namespace flakewm {
namespace view {
namespace {

QQmlEngine* Engine() {
  static auto* engine = new QQmlEngine(QCoreApplication::instance());
  return engine;
}

}  // namespace

WindowMenuRenderer::WindowMenuRenderer()
    : render_control_(std::make_unique<QQuickRenderControl>()),
      window_(std::make_unique<QQuickWindow>(render_control_.get())) {
  QQmlComponent component(Engine(), QUrl("qrc:/flakewm/window_menu.qml"));
  if (component.isError()) {
    ABSL_LOG(ERROR) << "Failed to load window menu QML: "
                    << component.errorString().toStdString();
    return;
  }
  QObject* object = component.create();
  root_item_ = qobject_cast<QQuickItem*>(object);
  if (root_item_ == nullptr) {
    ABSL_LOG(ERROR) << "Window menu QML root is not a QQuickItem";
    delete object;
    return;
  }
  root_item_->setParent(window_.get());
  root_item_->setParentItem(window_->contentItem());
  window_->setColor(Qt::transparent);
  window_->setGeometry(0, 0, kWidth, kHeight);
  window_->contentItem()->setSize(QSizeF(kWidth, kHeight));
  root_item_->setSize(QSizeF(kWidth, kHeight));
  SetProperty(
      "darkMode",
      QGuiApplication::palette().color(QPalette::Window).lightness() < 128);

  buffers_[0] = SsdBuffer::Create(kWidth, kHeight);
  buffers_[1] = SsdBuffer::Create(kWidth, kHeight);
  if (buffers_[0] == nullptr || buffers_[1] == nullptr) {
    DropBuffers();
    return;
  }
  window_->create();
  initialized_ = true;
  render_requested_ = QObject::connect(render_control_.get(),
                                       &QQuickRenderControl::renderRequested,
                                       [this]() { dirty_ = true; });
  scene_changed_ = QObject::connect(render_control_.get(),
                                    &QQuickRenderControl::sceneChanged,
                                    [this]() { dirty_ = true; });
}

WindowMenuRenderer::~WindowMenuRenderer() {
  QObject::disconnect(render_requested_);
  QObject::disconnect(scene_changed_);
  if (initialized_) render_control_->invalidate();
  DropBuffers();
}

bool WindowMenuRenderer::IsValid() const {
  return initialized_ && root_item_ != nullptr && buffers_[0] != nullptr;
}

void WindowMenuRenderer::SetItems(const QVariantList& items) {
  SetProperty("menuItems", items);
}

void WindowMenuRenderer::SetInteraction(int hovered_index, int pressed_index) {
  SetProperty("hoveredIndex", hovered_index);
  SetProperty("pressedIndex", pressed_index);
}

bool WindowMenuRenderer::Render() {
  if (!dirty_ || !IsValid()) return false;
  dirty_ = false;
  const int next = has_frame_ ? 1 - current_buffer_ : current_buffer_;
  SsdBuffer* buffer = buffers_[next];
  buffer->Image().fill(Qt::transparent);
  window_->setRenderTarget(
      QQuickRenderTarget::fromPaintDevice(&buffer->Image()));
  root_item_->update();
  render_control_->polishItems();
  render_control_->sync();
  render_control_->render();
  current_buffer_ = next;
  has_frame_ = true;
  return true;
}

wlr_buffer* WindowMenuRenderer::Buffer() const {
  return buffers_[current_buffer_] == nullptr
             ? nullptr
             : buffers_[current_buffer_]->Handle();
}

void WindowMenuRenderer::SetProperty(const char* name, const QVariant& value) {
  if (root_item_ == nullptr || root_item_->property(name) == value) return;
  root_item_->setProperty(name, value);
  dirty_ = true;
}

void WindowMenuRenderer::DropBuffers() {
  for (SsdBuffer*& buffer : buffers_) {
    if (buffer == nullptr) continue;
    wlr_buffer_drop(buffer->Handle());
    buffer = nullptr;
  }
}

}  // namespace view
}  // namespace flakewm
