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

#include <absl/log/absl_log.h>

#include <QCoreApplication>
#include <QImage>
#include <QMetaObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QUrl>
#include <QVariant>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <memory>

#include "src/view/ssd/ssd_buffer/ssd_buffer.h"
#include "src/view/touch_feedback.h"

namespace flakewm {
namespace view {
namespace {

constexpr int kFeedbackSize = 112;
constexpr int kFeedbackHalfSize = kFeedbackSize / 2;
constexpr int kReleasedLifetimeMs = 550;
constexpr double kHoldCancelDistance = 10.0;

bool IgnoreInput(wlr_scene_buffer*, double*, double*) { return false; }

QQmlEngine* Engine() {
  static auto* engine = new QQmlEngine(QCoreApplication::instance());
  return engine;
}

class TouchFeedbackRenderer final {
 public:
  TouchFeedbackRenderer()
      : render_control_(std::make_unique<QQuickRenderControl>()),
        window_(std::make_unique<QQuickWindow>(render_control_.get())) {
    QQmlComponent component(Engine(), QUrl("qrc:/flakewm/touch_feedback.qml"));
    if (component.isError()) {
      ABSL_LOG(ERROR) << "Failed to load touch feedback QML: "
                      << component.errorString().toStdString();
      return;
    }

    QObject* object = component.create();
    root_item_ = qobject_cast<QQuickItem*>(object);
    if (root_item_ == nullptr) {
      ABSL_LOG(ERROR) << "Touch feedback QML root is not a QQuickItem";
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

    buffers_[0] = SsdBuffer::Create(kFeedbackSize, kFeedbackSize);
    buffers_[1] = SsdBuffer::Create(kFeedbackSize, kFeedbackSize);
    if (buffers_[0] == nullptr || buffers_[1] == nullptr) {
      DropBuffers();
      return;
    }

    window_->setGeometry(0, 0, kFeedbackSize, kFeedbackSize);
    window_->contentItem()->setSize(QSizeF(kFeedbackSize, kFeedbackSize));
    root_item_->setSize(QSizeF(kFeedbackSize, kFeedbackSize));
    window_->create();
    initialized_ = true;
  }

  ~TouchFeedbackRenderer() {
    QObject::disconnect(render_requested_);
    QObject::disconnect(scene_changed_);
    if (initialized_) {
      render_control_->invalidate();
    }
    DropBuffers();
  }

  bool IsValid() const {
    return initialized_ && buffers_[0] != nullptr && buffers_[1] != nullptr;
  }

  void Begin() {
    SetProperty("moved", false);
    SetProperty("pressed", true);
    SetProperty("pulse", root_item_->property("pulse").toInt() + 1);
  }

  void SetMoved(bool moved) { SetProperty("moved", moved); }

  void End() { SetProperty("pressed", false); }

  bool Render() {
    if (!dirty_ || !IsValid()) {
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

  wlr_buffer* Buffer() const {
    return buffers_[current_buffer_] == nullptr
               ? nullptr
               : buffers_[current_buffer_]->Handle();
  }

 private:
  void SetProperty(const char* name, const QVariant& value) {
    if (root_item_ == nullptr || root_item_->property(name) == value) {
      return;
    }
    root_item_->setProperty(name, value);
    dirty_ = true;
  }

  void DropBuffers() {
    for (SsdBuffer*& buffer : buffers_) {
      if (buffer != nullptr) {
        wlr_buffer_drop(buffer->Handle());
        buffer = nullptr;
      }
    }
  }

  std::unique_ptr<QQuickRenderControl> render_control_;
  std::unique_ptr<QQuickWindow> window_;
  QQuickItem* root_item_ = nullptr;
  std::array<SsdBuffer*, 2> buffers_ = {};
  QMetaObject::Connection render_requested_;
  QMetaObject::Connection scene_changed_;
  int current_buffer_ = 0;
  bool has_frame_ = false;
  bool initialized_ = false;
  bool dirty_ = true;
};

}  // namespace

struct TouchFeedback::Point {
  ~Point() {
    if (node != nullptr) {
      wlr_scene_node_destroy(&node->node);
    }
  }

  wlr_touch* touch = nullptr;
  int32_t touch_id = 0;
  double start_x = 0;
  double start_y = 0;
  TouchFeedbackRenderer renderer;
  wlr_scene_buffer* node = nullptr;
  std::chrono::steady_clock::time_point released_at;
  bool released = false;
};

std::unique_ptr<TouchFeedback> TouchFeedback::Create(wlr_scene_tree* parent) {
  if (parent == nullptr) {
    return nullptr;
  }
  auto feedback = std::unique_ptr<TouchFeedback>(new TouchFeedback(parent));
  if (!feedback->IsValid()) {
    return nullptr;
  }
  return feedback;
}

TouchFeedback::TouchFeedback(wlr_scene_tree* parent) {
  tree_ = wlr_scene_tree_create(parent);
}

TouchFeedback::~TouchFeedback() {
  points_.clear();
  if (tree_ != nullptr) {
    wlr_scene_node_destroy(&tree_->node);
  }
}

void TouchFeedback::Down(wlr_touch* touch, int32_t touch_id,
                         Position position) {
  RemovePoint(touch, touch_id);
  auto point = std::make_unique<Point>();
  if (!point->renderer.IsValid()) {
    return;
  }
  point->touch = touch;
  point->touch_id = touch_id;
  point->start_x = position.x;
  point->start_y = position.y;
  point->renderer.Begin();
  if (!point->renderer.Render()) {
    return;
  }
  point->node = wlr_scene_buffer_create(tree_, point->renderer.Buffer());
  if (point->node == nullptr) {
    return;
  }
  point->node->point_accepts_input = IgnoreInput;
  wlr_scene_node_set_position(
      &point->node->node,
      static_cast<int>(std::round(position.x)) - kFeedbackHalfSize,
      static_cast<int>(std::round(position.y)) - kFeedbackHalfSize);
  points_.push_back(std::move(point));
}

void TouchFeedback::Motion(wlr_touch* touch, int32_t touch_id,
                           Position position) {
  Point* point = FindPoint(touch, touch_id);
  if (point == nullptr || point->released) {
    return;
  }
  const bool moved =
      std::hypot(position.x - point->start_x, position.y - point->start_y) >
      kHoldCancelDistance;
  point->renderer.SetMoved(moved);
  wlr_scene_node_set_position(
      &point->node->node,
      static_cast<int>(std::round(position.x)) - kFeedbackHalfSize,
      static_cast<int>(std::round(position.y)) - kFeedbackHalfSize);
}

void TouchFeedback::Up(wlr_touch* touch, int32_t touch_id) {
  Point* point = FindPoint(touch, touch_id);
  if (point == nullptr || point->released) {
    return;
  }
  point->released = true;
  point->released_at = std::chrono::steady_clock::now();
  point->renderer.End();
}

void TouchFeedback::Cancel(wlr_touch* touch, int32_t touch_id) {
  RemovePoint(touch, touch_id);
}

void TouchFeedback::CancelDevice(wlr_touch* touch) {
  std::erase_if(points_, [touch](const std::unique_ptr<Point>& point) {
    return point->touch == touch;
  });
}

void TouchFeedback::Render() {
  const auto now = std::chrono::steady_clock::now();
  std::erase_if(points_, [now](const std::unique_ptr<Point>& point) {
    return point->released &&
           now - point->released_at >=
               std::chrono::milliseconds(kReleasedLifetimeMs);
  });
  for (const std::unique_ptr<Point>& point : points_) {
    if (point->node != nullptr && point->renderer.Render()) {
      wlr_scene_buffer_set_buffer_with_damage(
          point->node, point->renderer.Buffer(), nullptr);
    }
  }
}

bool TouchFeedback::IsValid() const { return tree_ != nullptr; }

TouchFeedback::Point* TouchFeedback::FindPoint(wlr_touch* touch,
                                               int32_t touch_id) const {
  auto point = std::find_if(
      points_.begin(), points_.end(), [touch, touch_id](const auto& candidate) {
        return candidate->touch == touch && candidate->touch_id == touch_id;
      });
  return point == points_.end() ? nullptr : point->get();
}

void TouchFeedback::RemovePoint(wlr_touch* touch, int32_t touch_id) {
  std::erase_if(points_,
                [touch, touch_id](const std::unique_ptr<Point>& point) {
                  return point->touch == touch && point->touch_id == touch_id;
                });
}

}  // namespace view
}  // namespace flakewm
