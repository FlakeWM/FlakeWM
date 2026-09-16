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

#include <absl/log/absl_log.h>

#include <QCoreApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QUrl>

#include "src/view/ssd/popup_renderer/popup_renderer.h"
#include "src/view/ssd/ssd_buffer/ssd_buffer.h"

namespace flakewm {
namespace view {
namespace {

QQmlEngine* PopupEngine() {
  static auto* engine = new QQmlEngine(QCoreApplication::instance());
  return engine;
}

}  // namespace

PopupRenderer::PopupRenderer(const char* resource_url)
    : render_control_(std::make_unique<QQuickRenderControl>()),
      window_(std::make_unique<QQuickWindow>(render_control_.get())) {
  QQmlComponent component(PopupEngine(), QUrl(resource_url));
  if (component.isError()) {
    ABSL_LOG(ERROR) << "Failed to load SSD popup QML: "
                    << component.errorString().toStdString();
    return;
  }
  QObject* object = component.create();
  root_item_ = qobject_cast<QQuickItem*>(object);
  if (root_item_ == nullptr) {
    ABSL_LOG(ERROR) << "SSD popup QML root is not a QQuickItem";
    delete object;
    return;
  }
  root_item_->setParent(window_.get());
  root_item_->setParentItem(window_->contentItem());
  window_->setColor(Qt::transparent);
  window_->create();
  initialized_ = true;
  render_requested_ = QObject::connect(
      render_control_.get(), &QQuickRenderControl::renderRequested,
      [this]() { dirty_ = true; });
  scene_changed_ = QObject::connect(
      render_control_.get(), &QQuickRenderControl::sceneChanged,
      [this]() { dirty_ = true; });
}

PopupRenderer::~PopupRenderer() {
  QObject::disconnect(render_requested_);
  QObject::disconnect(scene_changed_);
  if (initialized_) render_control_->invalidate();
  DropBuffers();
}

bool PopupRenderer::IsValid() const {
  return initialized_ && root_item_ != nullptr;
}

bool PopupRenderer::Resize(int width, int height) {
  if (!IsValid() || width <= 0 || height <= 0) return false;
  if (width_ == width && height_ == height && buffers_[0] != nullptr) {
    return true;
  }
  DropBuffers();
  buffers_[0] = SsdBuffer::Create(width, height);
  buffers_[1] = SsdBuffer::Create(width, height);
  if (buffers_[0] == nullptr || buffers_[1] == nullptr) {
    DropBuffers();
    return false;
  }
  width_ = width;
  height_ = height;
  current_buffer_ = 0;
  has_frame_ = false;
  window_->setGeometry(0, 0, width, height);
  window_->contentItem()->setSize(QSizeF(width, height));
  root_item_->setSize(QSizeF(width, height));
  dirty_ = true;
  return true;
}

void PopupRenderer::SetProperty(const char* name, const QVariant& value) {
  if (root_item_ == nullptr || root_item_->property(name) == value) return;
  root_item_->setProperty(name, value);
  dirty_ = true;
}

bool PopupRenderer::Render() {
  if (!dirty_ || buffers_[0] == nullptr) return false;
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

wlr_buffer* PopupRenderer::Buffer() const {
  return buffers_[current_buffer_] == nullptr
             ? nullptr
             : buffers_[current_buffer_]->Handle();
}

void PopupRenderer::DropBuffers() {
  for (SsdBuffer*& buffer : buffers_) {
    if (buffer == nullptr) continue;
    wlr_buffer_drop(buffer->Handle());
    buffer = nullptr;
  }
}

}  // namespace view
}  // namespace flakewm
