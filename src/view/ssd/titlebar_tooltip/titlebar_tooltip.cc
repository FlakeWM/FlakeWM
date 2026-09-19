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

#include "src/view/ssd/titlebar_tooltip/titlebar_tooltip.h"

#include <QFont>
#include <QFontMetrics>
#include <QString>
#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

#include "src/view/ssd/popup_renderer/popup_animation.h"
#include "src/view/ssd/popup_renderer/popup_renderer.h"
#include "src/view/ssd/ssd_buffer/ssd_buffer.h"

namespace flakewm {
namespace view {
namespace {

constexpr int kShowDelayMs = 500;
constexpr int kAutoHideMs = 10000;
constexpr int kCursorOffset = 40;
// A tooltip is the same kind of surface as the popups gxde-wlcom blurs at
// (iterations 3, offset 0.60) in src/view/treeland_personalization.c, so it
// gets the same small offset.  The depth stays at the kernel default of 3.
constexpr float kBlurOffset = 0.60F;

bool IgnoreInput(wlr_scene_buffer*, double*, double*) { return false; }

QString HintText(TitlebarTooltip::Hint hint) {
  switch (hint) {
    case TitlebarTooltip::Hint::kMinimize:
      return QStringLiteral("Minimize");
    case TitlebarTooltip::Hint::kMaximize:
      return QStringLiteral("Maximize");
    case TitlebarTooltip::Hint::kRestore:
      return QStringLiteral("Restore");
    case TitlebarTooltip::Hint::kClose:
      return QStringLiteral("Close");
    case TitlebarTooltip::Hint::kNone:
      return {};
  }
  return {};
}

}  // namespace

TitlebarTooltip::TitlebarTooltip(wlr_scene_tree* overlay_parent,
                                 ScreenGeometry screen_geometry,
                                 SetBlur set_blur, ClearBlur clear_blur)
    : renderer_(std::make_unique<PopupRenderer>(
          "qrc:/flakewm/titlebar_tooltip/titlebar_tooltip.qml")),
      screen_geometry_(std::move(screen_geometry)),
      set_blur_(std::move(set_blur)),
      clear_blur_(std::move(clear_blur)) {
  animation_ =
      std::make_unique<PopupAnimation>([this](double value, bool finished) {
        ApplyAnimationFrame(value, finished);
      });
  show_timer_.setSingleShot(true);
  auto_hide_timer_.setSingleShot(true);
  QObject::connect(&show_timer_, &QTimer::timeout, [this]() { ShowNow(); });
  QObject::connect(&auto_hide_timer_, &QTimer::timeout,
                   [this]() { HideAnimated(); });
  if (overlay_parent == nullptr || renderer_ == nullptr ||
      !renderer_->IsValid()) {
    return;
  }
  tree_ = wlr_scene_tree_create(overlay_parent);
  if (tree_ == nullptr) return;
  blur_node_ = wlr_scene_buffer_create(tree_, nullptr);
  node_ = wlr_scene_buffer_create(tree_, nullptr);
  if (blur_node_ == nullptr || node_ == nullptr) {
    wlr_scene_node_destroy(&tree_->node);
    tree_ = nullptr;
    blur_node_ = nullptr;
    node_ = nullptr;
    return;
  }
  blur_node_->point_accepts_input = IgnoreInput;
  node_->point_accepts_input = IgnoreInput;
  blur_node_sample_.Connect(&blur_node_->events.output_sample);
  wlr_scene_node_set_enabled(&tree_->node, false);
}

TitlebarTooltip::~TitlebarTooltip() {
  show_timer_.stop();
  auto_hide_timer_.stop();
  animation_.reset();
  blur_node_sample_.Disconnect();
  ClearRegisteredBlur();
  if (tree_ != nullptr) wlr_scene_node_destroy(&tree_->node);
  if (blur_buffer_ != nullptr) wlr_buffer_drop(blur_buffer_->Handle());
}

void TitlebarTooltip::Hover(wlr_surface* surface, Hint hint, double cursor_x,
                            double cursor_y) {
  cursor_x_ = cursor_x;
  cursor_y_ = cursor_y;
  if (surface == nullptr || hint == Hint::kNone) {
    Cancel();
    return;
  }
  if (surface_ == surface && hint_ == hint &&
      (visible_ || show_timer_.isActive())) {
    return;
  }
  HideAnimated();
  surface_ = surface;
  hint_ = hint;
  show_timer_.start(kShowDelayMs);
}

void TitlebarTooltip::SurfaceUnavailable(wlr_surface* surface) {
  if (surface != nullptr && surface == surface_) Cancel();
}

void TitlebarTooltip::Cancel() {
  surface_ = nullptr;
  hint_ = Hint::kNone;
  show_timer_.stop();
  auto_hide_timer_.stop();
  HideAnimated();
}

void TitlebarTooltip::Render() {
  if (tree_ == nullptr || renderer_ == nullptr || !visible_) {
    return;
  }
  if (renderer_->Render()) {
    wlr_scene_buffer_set_buffer_with_damage(node_, renderer_->Buffer(),
                                            nullptr);
  }
}

void TitlebarTooltip::ShowNow() {
  if (tree_ == nullptr || surface_ == nullptr || hint_ == Hint::kNone ||
      !screen_geometry_) {
    return;
  }
  const QString text = HintText(hint_);
  QFont font(QStringLiteral("Source Han Sans SC"));
  font.setPixelSize(14);
  const QFontMetrics metrics(font);
  width_ = std::max(44, metrics.horizontalAdvance(text) + 18);
  height_ = std::max(28, metrics.height() + 10);
  if (!renderer_->Resize(width_, height_) ||
      !EnsureBlurBuffer(width_, height_)) {
    return;
  }
  renderer_->SetProperty("hintText", text);
  const wlr_box screen = screen_geometry_(cursor_x_, cursor_y_);
  if (screen.width <= 0 || screen.height <= 0) return;
  int x = static_cast<int>(std::lround(cursor_x_));
  int y = static_cast<int>(std::lround(cursor_y_)) + kCursorOffset;
  x = std::clamp(x, screen.x, screen.x + screen.width - width_);
  if (y + height_ > screen.y + screen.height) {
    y = static_cast<int>(std::lround(cursor_y_)) - height_;
  }
  x_ = x;
  y_ = y;
  wlr_scene_node_set_position(&blur_node_->node, x, y);
  wlr_scene_node_set_position(&node_->node, x, y);
  wlr_scene_node_set_enabled(&tree_->node, true);
  wlr_scene_node_raise_to_top(&tree_->node);
  visible_ = true;
  Render();
  if (animation_ != nullptr) animation_->Show();
  auto_hide_timer_.start(kAutoHideMs);
}

void TitlebarTooltip::HideAnimated() {
  show_timer_.stop();
  auto_hide_timer_.stop();
  if (!visible_ || renderer_ == nullptr) return;
  if (animation_ != nullptr) animation_->Hide();
}

void TitlebarTooltip::FinishHide() {
  visible_ = false;
  if (tree_ != nullptr) wlr_scene_node_set_enabled(&tree_->node, false);
  ClearRegisteredBlur();
}

void TitlebarTooltip::ApplyAnimationFrame(double value, bool finished) {
  if (!visible_ || blur_node_ == nullptr || node_ == nullptr) return;
  const double scale = 0.85 + value * 0.15;
  const int width = std::max(1, static_cast<int>(std::lround(width_ * scale)));
  const int height =
      std::max(1, static_cast<int>(std::lround(height_ * scale)));
  const int target_x = x_ + (width_ - width) / 2;
  const int target_y = y_ + (height_ - height) / 2;
  wlr_scene_buffer_set_dest_size(blur_node_, width, height);
  wlr_scene_buffer_set_dest_size(node_, width, height);
  wlr_scene_buffer_set_opacity(blur_node_, static_cast<float>(value));
  wlr_scene_buffer_set_opacity(node_, static_cast<float>(value));
  wlr_scene_node_set_position(&blur_node_->node, target_x, target_y);
  wlr_scene_node_set_position(&node_->node, target_x, target_y);
  if (finished && value <= 0.0) FinishHide();
}

bool TitlebarTooltip::EnsureBlurBuffer(int width, int height) {
  if (blur_node_ == nullptr) return false;
  if (blur_buffer_ != nullptr && blur_buffer_->Image().width() == width &&
      blur_buffer_->Image().height() == height) {
    return true;
  }
  ClearRegisteredBlur();
  if (blur_buffer_ != nullptr) wlr_buffer_drop(blur_buffer_->Handle());
  blur_buffer_ = SsdBuffer::Create(width, height);
  if (blur_buffer_ == nullptr) return false;
  blur_buffer_->Image().fill(Qt::transparent);
  wlr_scene_buffer_set_buffer(blur_node_, blur_buffer_->Handle());
  return true;
}

void TitlebarTooltip::ClearRegisteredBlur() {
  if (registered_blur_texture_ == nullptr) return;
  registered_blur_texture_ = nullptr;
  if (clear_blur_) clear_blur_(this);
}

void TitlebarTooltip::OnBlurNodeSample(TitlebarTooltip* tooltip,
                                       wlr_scene_output_sample_event*) {
  if (!tooltip->visible_ || !tooltip->set_blur_ ||
      tooltip->blur_node_ == nullptr) {
    return;
  }
  wlr_texture* texture = tooltip->blur_node_->WLR_PRIVATE.texture;
  if (texture == nullptr || texture == tooltip->registered_blur_texture_)
    return;
  pixman_region32_t region;
  pixman_region32_init_rect(&region, 0, 0, tooltip->width_, tooltip->height_);
  if (tooltip->set_blur_(tooltip, texture, &region, kBlurOffset)) {
    tooltip->registered_blur_texture_ = texture;
  }
  pixman_region32_fini(&region);
}

}  // namespace view
}  // namespace flakewm
