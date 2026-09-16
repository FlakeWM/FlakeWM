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

#include <linux/input-event-codes.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <utility>

#include "src/view/ssd/split_screen_switcher/split_screen_switcher.h"
#include "src/view/ssd/popup_renderer/popup_animation.h"
#include "src/view/ssd/popup_renderer/popup_renderer.h"
#include "src/view/ssd/ssd_buffer/ssd_buffer.h"

namespace flakewm {
namespace view {
namespace {

constexpr int kWidth = 204;
constexpr int kHeight = 80;
constexpr int kShowDelayMs = 500;
constexpr float kBlurOffset = 3.0F;

struct ItemRect {
  int x;
  int y;
  int width;
  int height;
};

// 90x64 cards at (8,8) and (106,8), with their item rectangles inset by 5.
constexpr std::array<ItemRect, 6> kItemRects = {{
    {13, 13, 40, 56},
    {55, 13, 40, 56},
    {111, 13, 40, 27},
    {111, 42, 40, 27},
    {153, 13, 40, 27},
    {153, 42, 40, 27},
}};

constexpr std::array<SplitScreenSwitcher::Tile, 6> kTiles = {
    SplitScreenSwitcher::Tile::kLeft,
    SplitScreenSwitcher::Tile::kRight,
    SplitScreenSwitcher::Tile::kTopLeft,
    SplitScreenSwitcher::Tile::kBottomLeft,
    SplitScreenSwitcher::Tile::kTopRight,
    SplitScreenSwitcher::Tile::kBottomRight,
};

bool IgnoreInput(wlr_scene_buffer*, double*, double*) { return false; }

}  // namespace

SplitScreenSwitcher::SplitScreenSwitcher(
    wlr_scene_tree* overlay_parent, Activate activate,
    ScreenGeometry screen_geometry, SetBlur set_blur, ClearBlur clear_blur)
    : renderer_(std::make_unique<PopupRenderer>(
          "qrc:/flakewm/split_screen_switcher/split_screen_switcher.qml")),
      activate_(std::move(activate)),
      screen_geometry_(std::move(screen_geometry)),
      set_blur_(std::move(set_blur)),
      clear_blur_(std::move(clear_blur)) {
  animation_ = std::make_unique<PopupAnimation>(
      [this](double value, bool finished) {
        ApplyAnimationFrame(value, finished);
      });
  show_timer_.setSingleShot(true);
  QObject::connect(&show_timer_, &QTimer::timeout, [this]() { ShowNow(); });
  if (overlay_parent == nullptr || renderer_ == nullptr ||
      !renderer_->IsValid() || !renderer_->Resize(kWidth, kHeight)) {
    return;
  }
  tree_ = wlr_scene_tree_create(overlay_parent);
  if (tree_ == nullptr) return;
  blur_node_ = wlr_scene_buffer_create(tree_, nullptr);
  node_ = wlr_scene_buffer_create(tree_, nullptr);
  if (blur_node_ == nullptr || node_ == nullptr || !EnsureBlurBuffer()) {
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

SplitScreenSwitcher::~SplitScreenSwitcher() {
  show_timer_.stop();
  animation_.reset();
  blur_node_sample_.Disconnect();
  ClearRegisteredBlur();
  if (tree_ != nullptr) wlr_scene_node_destroy(&tree_->node);
  if (blur_buffer_ != nullptr) wlr_buffer_drop(blur_buffer_->Handle());
}

void SplitScreenSwitcher::HoverMaximize(wlr_surface* surface, double cursor_x,
                                        double cursor_y, int client_top) {
  cursor_x_ = cursor_x;
  cursor_y_ = cursor_y;
  client_top_ = client_top;
  if (surface == nullptr) {
    LeaveMaximize();
    return;
  }
  if (surface_ == surface && (active_ || show_timer_.isActive())) return;
  Cancel();
  surface_ = surface;
  show_timer_.start(kShowDelayMs);
}

void SplitScreenSwitcher::LeaveMaximize() { Cancel(); }

bool SplitScreenSwitcher::HandleMotion(double x, double y) {
  if (!active_) return false;
  if (x < x_ || y < y_ || x >= x_ + kWidth || y >= y_ + kHeight) {
    pointer_inside_ = false;
    return false;
  }
  pointer_inside_ = true;
  const int hovered = ItemAt(x, y);
  if (hovered != hovered_item_) {
    hovered_item_ = hovered;
    UpdateView();
  }
  return true;
}

bool SplitScreenSwitcher::HandleButton(uint32_t button,
                                       wl_pointer_button_state state) {
  if (!active_ || !pointer_inside_) return false;
  if (button != BTN_LEFT) {
    if (state == WL_POINTER_BUTTON_STATE_PRESSED) Cancel();
    return true;
  }
  if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
    pressed_item_ = hovered_item_;
    UpdateView();
    return true;
  }
  const int selected = pressed_item_ >= 0 && pressed_item_ == hovered_item_
                           ? pressed_item_
                           : -1;
  pressed_item_ = -1;
  pointer_inside_ = false;
  if (selected >= 0 && selected < static_cast<int>(kTiles.size()) &&
      surface_ != nullptr) {
    wlr_surface* surface = surface_;
    const Tile tile = kTiles[selected];
    Cancel();
    if (activate_) activate_(surface, tile);
  } else {
    UpdateView();
  }
  return true;
}

void SplitScreenSwitcher::SurfaceUnavailable(wlr_surface* surface) {
  if (surface != nullptr && surface == surface_) Cancel();
}

void SplitScreenSwitcher::Cancel() {
  show_timer_.stop();
  surface_ = nullptr;
  active_ = false;
  hovered_item_ = -1;
  pressed_item_ = -1;
  HideAnimated();
}

void SplitScreenSwitcher::Render() {
  if (!visible_ || renderer_ == nullptr || node_ == nullptr) return;
  if (renderer_->Render()) {
    wlr_scene_buffer_set_buffer_with_damage(node_, renderer_->Buffer(),
                                            nullptr);
  }
}

bool SplitScreenSwitcher::IsVisible() const { return visible_; }

void SplitScreenSwitcher::ShowNow() {
  if (tree_ == nullptr || surface_ == nullptr || !screen_geometry_) return;
  const wlr_box screen = screen_geometry_(cursor_x_, cursor_y_);
  if (screen.width <= 0 || screen.height <= 0) return;
  int x = static_cast<int>(std::lround(cursor_x_));
  if (x > 80) x -= 80;
  x = std::clamp(x, screen.x, screen.x + screen.width - kWidth);
  int y = client_top_ - 6;
  y = std::clamp(y, screen.y, screen.y + screen.height - kHeight);
  x_ = x;
  y_ = y;
  hovered_item_ = ItemAt(cursor_x_, cursor_y_);
  pointer_inside_ = cursor_x_ >= x_ && cursor_y_ >= y_ &&
                    cursor_x_ < x_ + kWidth && cursor_y_ < y_ + kHeight;
  pressed_item_ = -1;
  active_ = true;
  visible_ = true;
  UpdateView();
  if (animation_ != nullptr) animation_->Show();
}

void SplitScreenSwitcher::UpdateView() {
  if (!visible_ || tree_ == nullptr || renderer_ == nullptr) return;
  renderer_->SetProperty("hoveredItem", hovered_item_);
  renderer_->SetProperty("pressedItem", pressed_item_);
  renderer_->SetProperty("pointerInside", pointer_inside_);
  Render();
  wlr_scene_node_set_enabled(&tree_->node, true);
  wlr_scene_node_raise_to_top(&tree_->node);
  ApplyAnimationFrame(animation_value_, false);
}

void SplitScreenSwitcher::HideAnimated() {
  if (!visible_ || renderer_ == nullptr) return;
  renderer_->SetProperty("pointerInside", false);
  if (animation_ != nullptr) animation_->Hide();
}

void SplitScreenSwitcher::FinishHide() {
  visible_ = false;
  if (tree_ != nullptr) wlr_scene_node_set_enabled(&tree_->node, false);
  ClearRegisteredBlur();
}

void SplitScreenSwitcher::ApplyAnimationFrame(double value, bool finished) {
  if (!visible_ || blur_node_ == nullptr || node_ == nullptr) return;
  animation_value_ = value;
  const double scale = 0.85 + value * 0.15;
  const int width = std::max(1, static_cast<int>(std::lround(kWidth * scale)));
  const int height =
      std::max(1, static_cast<int>(std::lround(kHeight * scale)));
  const int target_x = x_ + (kWidth - width) / 2;
  wlr_scene_buffer_set_dest_size(blur_node_, width, height);
  wlr_scene_buffer_set_dest_size(node_, width, height);
  wlr_scene_buffer_set_opacity(blur_node_, static_cast<float>(value));
  wlr_scene_buffer_set_opacity(node_, static_cast<float>(value));
  wlr_scene_node_set_position(&blur_node_->node, target_x, y_);
  wlr_scene_node_set_position(&node_->node, target_x, y_);
  if (finished && value <= 0.0) FinishHide();
}

int SplitScreenSwitcher::ItemAt(double x, double y) const {
  const int local_x = static_cast<int>(std::floor(x - x_));
  const int local_y = static_cast<int>(std::floor(y - y_));
  for (std::size_t index = 0; index < kItemRects.size(); ++index) {
    const ItemRect& item = kItemRects[index];
    if (local_x >= item.x && local_y >= item.y &&
        local_x < item.x + item.width && local_y < item.y + item.height) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

bool SplitScreenSwitcher::EnsureBlurBuffer() {
  if (blur_node_ == nullptr) return false;
  blur_buffer_ = SsdBuffer::Create(kWidth, kHeight);
  if (blur_buffer_ == nullptr) return false;
  blur_buffer_->Image().fill(Qt::transparent);
  wlr_scene_buffer_set_buffer(blur_node_, blur_buffer_->Handle());
  return true;
}

void SplitScreenSwitcher::ClearRegisteredBlur() {
  if (registered_blur_texture_ == nullptr) return;
  registered_blur_texture_ = nullptr;
  if (clear_blur_) clear_blur_(this);
}

void SplitScreenSwitcher::OnBlurNodeSample(
    SplitScreenSwitcher* switcher, wlr_scene_output_sample_event*) {
  if (!switcher->visible_ || !switcher->set_blur_ ||
      switcher->blur_node_ == nullptr) {
    return;
  }
  wlr_texture* texture = switcher->blur_node_->WLR_PRIVATE.texture;
  if (texture == nullptr || texture == switcher->registered_blur_texture_)
    return;
  pixman_region32_t region;
  pixman_region32_init_rect(&region, 1, 1, kWidth - 2, kHeight - 2);
  if (switcher->set_blur_(switcher, texture, &region, kBlurOffset)) {
    switcher->registered_blur_texture_ = texture;
  }
  pixman_region32_fini(&region);
}

}  // namespace view
}  // namespace flakewm
