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
 * The layout is adapted from GXDE KWin's Multitasking screen.
 */

#include <absl/log/absl_log.h>

#include <QCoreApplication>
#include <QIcon>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QUrl>
#include <QVariant>

#include "src/view/ssd/ssd_buffer/ssd_buffer.h"
#include "src/view/ssd/window_icon_provider/window_icon_provider.h"
#include "src/view/multitasking/multitasking_renderer.h"

namespace flakewm {
namespace view {
namespace {

QQmlEngine* Engine() {
  static auto* engine = [] {
    if (QIcon::themeName().isEmpty()) QIcon::setThemeName("hicolor");
    QIcon::setFallbackThemeName("hicolor");
    auto* result = new QQmlEngine(QCoreApplication::instance());
    result->addImageProvider("multitasking-icons", new WindowIconProvider());
    return result;
  }();
  return engine;
}

}  // namespace

MultitaskingRenderer::MultitaskingRenderer()
    : render_control_(std::make_unique<QQuickRenderControl>()),
      window_(std::make_unique<QQuickWindow>(render_control_.get())) {
  QQmlComponent component(Engine(), QUrl("qrc:/flakewm/multitasking.qml"));
  if (component.isError()) {
    ABSL_LOG(ERROR) << "Failed to load multitasking QML: "
                    << component.errorString().toStdString();
    return;
  }
  QObject* object = component.create();
  root_item_ = qobject_cast<QQuickItem*>(object);
  if (root_item_ == nullptr) {
    ABSL_LOG(ERROR) << "Multitasking QML root is not a QQuickItem";
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

MultitaskingRenderer::~MultitaskingRenderer() {
  QObject::disconnect(render_requested_);
  QObject::disconnect(scene_changed_);
  if (initialized_) render_control_->invalidate();
  DropBuffers();
}

bool MultitaskingRenderer::Resize(int width, int height) {
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

void MultitaskingRenderer::SetState(
    const std::vector<Multitasking::Entry>& entries,
    const std::vector<int>& workspace_window_counts,
    const std::vector<MultitaskingWindowPlacement>& placements,
    int current_workspace, int selected_workspace, int selected_window,
    int hovered_workspace, int hovered_window, double workspace_opacity,
    const MultitaskingLayout& layout) {
  QVariantList windows;
  windows.reserve(static_cast<qsizetype>(entries.size()));
  for (std::size_t index = 0; index < entries.size(); ++index) {
    const Multitasking::Entry& entry = entries[index];
    const MultitaskingWindowPlacement placement =
        index < placements.size() ? placements[index]
                                  : MultitaskingWindowPlacement{};
    windows.push_back(
        QVariantMap{{QStringLiteral("title"), entry.title},
                    {QStringLiteral("appId"), entry.app_id},
                    {QStringLiteral("active"), entry.active},
                    {QStringLiteral("minimized"), entry.minimized},
                    {QStringLiteral("keptAbove"), entry.kept_above},
                    {QStringLiteral("x"), placement.x},
                    {QStringLiteral("y"), placement.y},
                    {QStringLiteral("width"), placement.width},
                    {QStringLiteral("height"), placement.height}});
  }
  QVariantList workspaces;
  workspaces.reserve(static_cast<qsizetype>(workspace_window_counts.size()));
  for (qsizetype index = 0;
       index < static_cast<qsizetype>(workspace_window_counts.size());
       ++index) {
    workspaces.push_back(QVariantMap{
        {QStringLiteral("number"), index + 1},
        {QStringLiteral("count"),
         workspace_window_counts[static_cast<std::size_t>(index)]}});
  }
  SetProperty("entries", windows);
  SetProperty("workspaces", workspaces);
  SetProperty("canAddWorkspace", workspace_window_counts.size() < 6);
  SetProperty("currentWorkspace", current_workspace);
  SetProperty("selectedWorkspace", selected_workspace);
  SetProperty("selectedWindow", selected_window);
  SetProperty("hoveredWorkspace", hovered_workspace);
  SetProperty("hoveredWindow", hovered_window);
  SetProperty("workspaceOpacity", workspace_opacity);
  SetProperty("workspaceX", layout.workspace_x);
  SetProperty("workspaceY", layout.workspace_y);
  SetProperty("workspaceWidth", layout.workspace_width);
  SetProperty("workspaceHeight", layout.workspace_height);
  SetProperty("workspaceGap", layout.workspace_gap);
  SetProperty("windowAreaY", layout.window_area_y);
  SetProperty("addX", layout.add_x);
  SetProperty("addY", layout.add_y);
  SetProperty("addSize", layout.add_size);
  SetProperty("draggedWorkspace", layout.dragged_workspace);
  SetProperty("draggedWorkspaceX", layout.dragged_workspace_x);
}

bool MultitaskingRenderer::Render() {
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

wlr_buffer* MultitaskingRenderer::Buffer() const {
  return buffers_[current_buffer_] == nullptr
             ? nullptr
             : buffers_[current_buffer_]->Handle();
}

void MultitaskingRenderer::SetProperty(const char* name,
                                       const QVariant& value) {
  if (root_item_ == nullptr || root_item_->property(name) == value) return;
  root_item_->setProperty(name, value);
  dirty_ = true;
}

void MultitaskingRenderer::DropBuffers() {
  for (SsdBuffer*& buffer : buffers_) {
    if (buffer == nullptr) continue;
    wlr_buffer_drop(buffer->Handle());
    buffer = nullptr;
  }
}

}  // namespace view
}  // namespace flakewm
