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
 * The file is adapted from GXDE KWin's Window Preview screen.
 */

#include "src/view/window_previews/window_previews_renderer.h"

#include <absl/log/absl_log.h>

#include <QCoreApplication>
#include <QIcon>
#include <QPainter>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QUrl>
#include <QVariant>
#include <algorithm>

#include "src/view/ssd/ssd_buffer/ssd_buffer.h"
#include "src/view/ssd/window_icon_provider/window_icon_provider.h"

namespace flakewm {
namespace view {
namespace {

class WindowPreviewIconProvider final : public QQuickImageProvider {
 public:
  WindowPreviewIconProvider() : QQuickImageProvider(Image) {}

  QImage requestImage(const QString& id, QSize* size,
                      const QSize& requested_size) override {
    QImage image = provider_.requestImage(id, size, requested_size);
    if (!image.isNull()) return image;
    const int extent =
        requested_size.isValid() ? std::max(1, requested_size.width()) : 64;
    image = QImage(extent, extent, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(50, 116, 205));
    painter.drawRoundedRect(QRectF(0, 0, extent, extent), extent * 0.18,
                            extent * 0.18);
    painter.setPen(Qt::white);
    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(std::max(12, extent / 2));
    painter.setFont(font);
    const QString glyph =
        id.isEmpty() ? QStringLiteral("•") : id.left(1).toUpper();
    painter.drawText(image.rect(), Qt::AlignCenter, glyph);
    if (size != nullptr) *size = image.size();
    return image;
  }

 private:
  WindowIconProvider provider_;
};

QQmlEngine* Engine() {
  static auto* engine = [] {
    if (QIcon::themeName().isEmpty()) QIcon::setThemeName("hicolor");
    QIcon::setFallbackThemeName("hicolor");
    auto* result = new QQmlEngine(QCoreApplication::instance());
    result->addImageProvider("window-preview-icons",
                             new WindowPreviewIconProvider());
    return result;
  }();
  return engine;
}

}  // namespace

WindowPreviewsRenderer::WindowPreviewsRenderer()
    : render_control_(std::make_unique<QQuickRenderControl>()),
      window_(std::make_unique<QQuickWindow>(render_control_.get())) {
  QQmlComponent component(Engine(), QUrl("qrc:/flakewm/window_previews.qml"));
  if (component.isError()) {
    ABSL_LOG(ERROR) << "Failed to load window previews QML: "
                    << component.errorString().toStdString();
    return;
  }
  QObject* object = component.create();
  root_item_ = qobject_cast<QQuickItem*>(object);
  if (root_item_ == nullptr) {
    ABSL_LOG(ERROR) << "Window previews QML root is not a QQuickItem";
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

WindowPreviewsRenderer::~WindowPreviewsRenderer() {
  QObject::disconnect(render_requested_);
  QObject::disconnect(scene_changed_);
  if (initialized_) render_control_->invalidate();
  DropBuffers();
}

bool WindowPreviewsRenderer::Resize(int width, int height) {
  if (root_item_ == nullptr || width <= 0 || height <= 0) return false;
  if (buffers_[0] != nullptr && buffers_[0]->Handle()->width == width &&
      buffers_[0]->Handle()->height == height) {
    return true;
  }
  std::array<SsdBuffer*, 2> next = {SsdBuffer::Create(width, height),
                                    SsdBuffer::Create(width, height)};
  if (next[0] == nullptr || next[1] == nullptr) {
    for (SsdBuffer* buffer : next) {
      if (buffer != nullptr) wlr_buffer_drop(buffer->Handle());
    }
    return false;
  }
  DropBuffers();
  buffers_ = next;
  current_buffer_ = 0;
  has_frame_ = false;
  window_->setGeometry(0, 0, width, height);
  window_->contentItem()->setSize(QSizeF(width, height));
  root_item_->setSize(QSizeF(width, height));
  if (!initialized_) {
    window_->create();
    initialized_ = true;
  }
  dirty_ = true;
  return true;
}

void WindowPreviewsRenderer::SetState(
    const std::vector<WindowPreviews::Entry>& entries,
    const std::vector<WindowPreviewVisual>& visuals, const QString& filter,
    double decal_opacity) {
  QVariantList windows;
  windows.reserve(static_cast<qsizetype>(entries.size()));
  for (std::size_t index = 0; index < entries.size(); ++index) {
    const WindowPreviewVisual visual =
        index < visuals.size() ? visuals[index] : WindowPreviewVisual{};
    windows.push_back(QVariantMap{
        {QStringLiteral("title"), entries[index].title},
        {QStringLiteral("appId"), entries[index].app_id},
        {QStringLiteral("x"), visual.x},
        {QStringLiteral("y"), visual.y},
        {QStringLiteral("width"), visual.width},
        {QStringLiteral("height"), visual.height},
        {QStringLiteral("baseX"), visual.base_x},
        {QStringLiteral("baseY"), visual.base_y},
        {QStringLiteral("baseWidth"), visual.base_width},
        {QStringLiteral("baseHeight"), visual.base_height},
        {QStringLiteral("closeX"), visual.close_x},
        {QStringLiteral("closeY"), visual.close_y},
        {QStringLiteral("opacity"), visual.opacity},
        {QStringLiteral("highlight"), visual.highlight},
        {QStringLiteral("showClose"), visual.show_close},
    });
  }
  SetProperty("entries", windows);
  SetProperty("filterText", filter);
  SetProperty("decalOpacity", decal_opacity);
}

bool WindowPreviewsRenderer::Render() {
  if (!dirty_ || !initialized_ || buffers_[0] == nullptr) return false;
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

wlr_buffer* WindowPreviewsRenderer::Buffer() const {
  return buffers_[current_buffer_] == nullptr
             ? nullptr
             : buffers_[current_buffer_]->Handle();
}

void WindowPreviewsRenderer::SetProperty(const char* name,
                                         const QVariant& value) {
  if (root_item_ == nullptr || root_item_->property(name) == value) return;
  root_item_->setProperty(name, value);
  dirty_ = true;
}

void WindowPreviewsRenderer::DropBuffers() {
  for (SsdBuffer*& buffer : buffers_) {
    if (buffer == nullptr) continue;
    wlr_buffer_drop(buffer->Handle());
    buffer = nullptr;
  }
}

}  // namespace view
}  // namespace flakewm
