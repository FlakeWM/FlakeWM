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
 * QtQuick renderer for an SSD titlebar.
 */

#include "src/view/ssd/ssd_renderer/ssd_renderer.h"

#include <absl/log/absl_log.h>

#include <QCoreApplication>
#include <QIcon>
#include <QImage>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QUrl>
#include <QVariant>
#include <utility>

#include "src/view/ssd/window_icon_provider/window_icon_provider.h"

namespace flakewm {
namespace view {

QQmlEngine* SsdRenderer::Engine() {
  static auto* engine = [] {
    if (QIcon::themeName().isEmpty()) {
      QIcon::setThemeName("hicolor");
    }
    QIcon::setFallbackThemeName("hicolor");
    auto* result = new QQmlEngine(QCoreApplication::instance());
    result->addImageProvider("window-icons", new WindowIconProvider());
    return result;
  }();
  return engine;
}

SsdRenderer::SsdRenderer()
    : render_control_(std::make_unique<QQuickRenderControl>()),
      window_(std::make_unique<QQuickWindow>(render_control_.get())) {
  QQmlComponent component(Engine(), QUrl("qrc:/flakewm/ssd.qml"));
  if (component.isError()) {
    ABSL_LOG(ERROR) << "Failed to load SSD QML: "
                    << component.errorString().toStdString();
    return;
  }

  QObject* object = component.create();
  root_item_ = qobject_cast<QQuickItem*>(object);
  if (root_item_ == nullptr) {
    ABSL_LOG(ERROR) << "SSD QML root is not a QQuickItem";
    delete object;
    return;
  }
  root_item_->setParent(window_.get());
  root_item_->setParentItem(window_->contentItem());
  window_->setColor(Qt::transparent);

  render_requested_ = QObject::connect(render_control_.get(),
                                       &QQuickRenderControl::renderRequested,
                                       [this]() { dirty_ = true; });
  scene_changed_ = QObject::connect(render_control_.get(),
                                    &QQuickRenderControl::sceneChanged,
                                    [this]() { dirty_ = true; });
}

SsdRenderer::~SsdRenderer() {
  QObject::disconnect(render_requested_);
  QObject::disconnect(scene_changed_);
  if (initialized_) {
    render_control_->invalidate();
  }
  DropBuffers();
}

bool SsdRenderer::IsValid() const { return root_item_ != nullptr; }

bool SsdRenderer::Resize(int width, int height) {
  if (!IsValid() || width <= 0 || height <= 0) {
    return false;
  }
  if (buffers_[0] != nullptr && buffers_[0]->Handle()->width == width &&
      buffers_[0]->Handle()->height == height) {
    return true;
  }

  std::array<SsdBuffer*, 2> next_buffers = {SsdBuffer::Create(width, height),
                                            SsdBuffer::Create(width, height)};
  if (next_buffers[0] == nullptr || next_buffers[1] == nullptr) {
    for (SsdBuffer* buffer : next_buffers) {
      if (buffer != nullptr) {
        wlr_buffer_drop(buffer->Handle());
      }
    }
    return false;
  }
  DropBuffers();
  buffers_ = next_buffers;
  current_buffer_ = 0;
  has_frame_ = false;

  window_->setGeometry(0, 0, width, height);
  window_->contentItem()->setSize(QSizeF(width, height));
  root_item_->setSize(QSizeF(width, height));
  if (!initialized_) {
    // The software adaptation is initialized by creating its QQuickWindow.
    // QQuickRenderControl::initialize() only initializes an RHI backend and
    // therefore returns false for a QImage render target.
    window_->create();
    initialized_ = true;
  }
  dirty_ = true;
  return true;
}

void SsdRenderer::SetActive(bool active) { SetProperty("active", active); }

void SsdRenderer::SetMaximized(bool maximized) {
  SetProperty("maximized", maximized);
}

void SsdRenderer::SetDialog(bool dialog) { SetProperty("dialog", dialog); }

void SsdRenderer::SetCanMinimize(bool can_minimize) {
  SetProperty("canMinimize", can_minimize);
}

void SsdRenderer::SetCanMaximize(bool can_maximize) {
  SetProperty("canMaximize", can_maximize);
}

void SsdRenderer::SetTitle(const QString& title) {
  SetProperty("title", title);
}

void SsdRenderer::SetAppId(const QString& app_id) {
  SetProperty("appId", app_id);
}

void SsdRenderer::SetHoveredPart(int part) { SetProperty("hoveredPart", part); }

void SsdRenderer::SetPressedPart(int part) { SetProperty("pressedPart", part); }

bool SsdRenderer::Render() {
  if (!dirty_ || !initialized_ || buffers_[0] == nullptr) {
    return false;
  }

  dirty_ = false;
  const int next_buffer = has_frame_ ? 1 - current_buffer_ : current_buffer_;
  SsdBuffer* buffer = buffers_[next_buffer];
  buffer->Image().fill(Qt::transparent);
  window_->setRenderTarget(
      QQuickRenderTarget::fromPaintDevice(&buffer->Image()));
  root_item_->update();
  render_control_->polishItems();
  render_control_->sync();
  render_control_->render();
  current_buffer_ = next_buffer;
  has_frame_ = true;
  return true;
}

wlr_buffer* SsdRenderer::Buffer() const {
  return buffers_[current_buffer_] == nullptr
             ? nullptr
             : buffers_[current_buffer_]->Handle();
}

void SsdRenderer::DropBuffers() {
  for (SsdBuffer*& buffer : buffers_) {
    if (buffer != nullptr) {
      wlr_buffer_drop(buffer->Handle());
      buffer = nullptr;
    }
  }
}

void SsdRenderer::SetProperty(const char* name, const QVariant& value) {
  if (root_item_ == nullptr || root_item_->property(name) == value) {
    return;
  }
  root_item_->setProperty(name, value);
  dirty_ = true;
}

}  // namespace view
}  // namespace flakewm
