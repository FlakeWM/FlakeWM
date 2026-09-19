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

#include "src/view/ssd/window_menu/window_menu.h"

#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include <QVariantMap>
#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include "src/view/ssd/ssd_buffer/ssd_buffer.h"
#include "src/view/ssd/window_menu/window_menu_renderer.h"

namespace flakewm {
namespace view {
namespace {

// Menus in gxde-wlcom are blurred at (iterations 3, offset 0.60), per
// src/widget/menu.c, which notes that a bigger offset is a coarser pass and
// shows against a background this translucent.  The depth stays at the kernel
// default of 3.
constexpr float kBlurOffset = 0.60F;

bool IgnoreInput(wlr_scene_buffer*, double*, double*) { return false; }

const std::array<const char*, WindowMenuRenderer::kItemCount> kLabels = {
    "Minimize",
    "Maximize",
    "Move",
    "Resize",
    "Always on Top",
    "Always on Visible Workspace",
    "Move to Workspace Left",
    "Move to Workspace Right",
    "Close",
};

}  // namespace

WindowMenu::WindowMenu(wlr_scene_tree* overlay_parent,
                       StateProvider state_provider,
                       ActionHandler action_handler,
                       ScreenGeometry screen_geometry, SetBlur set_blur,
                       ClearBlur clear_blur)
    : renderer_(std::make_unique<WindowMenuRenderer>()),
      state_provider_(std::move(state_provider)),
      action_handler_(std::move(action_handler)),
      screen_geometry_(std::move(screen_geometry)),
      set_blur_(std::move(set_blur)),
      clear_blur_(std::move(clear_blur)) {
  if (overlay_parent == nullptr || renderer_ == nullptr ||
      !renderer_->IsValid()) {
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

WindowMenu::~WindowMenu() {
  active_ = false;
  surface_ = nullptr;
  blur_node_sample_.Disconnect();
  ClearRegisteredBlur();
  if (tree_ != nullptr) {
    wlr_scene_node_destroy(&tree_->node);
    tree_ = nullptr;
    blur_node_ = nullptr;
    node_ = nullptr;
  }
  if (blur_buffer_ != nullptr) {
    wlr_buffer_drop(blur_buffer_->Handle());
    blur_buffer_ = nullptr;
  }
  renderer_.reset();
}

bool WindowMenu::Show(wlr_surface* surface, double x, double y) {
  if (surface == nullptr || tree_ == nullptr || !state_provider_ ||
      !screen_geometry_) {
    return false;
  }
  const std::optional<State> state = state_provider_(surface);
  if (!state.has_value()) return false;

  surface_ = surface;
  RebuildItems(*state);
  const wlr_box screen = screen_geometry_(x, y);
  if (screen.width <= 0 || screen.height <= 0) return false;
  const int minimum_x = screen.x - WindowMenuRenderer::kShadowMargin;
  const int minimum_y = screen.y - WindowMenuRenderer::kShadowMargin;
  const int maximum_x = screen.x + screen.width - WindowMenuRenderer::kWidth;
  const int maximum_y = screen.y + screen.height - WindowMenuRenderer::kHeight;
  x_ = std::clamp(
      static_cast<int>(std::lround(x)) - WindowMenuRenderer::kShadowMargin,
      minimum_x, std::max(minimum_x, maximum_x));
  y_ = std::clamp(
      static_cast<int>(std::lround(y)) - WindowMenuRenderer::kShadowMargin,
      minimum_y, std::max(minimum_y, maximum_y));
  pointer_target_ = ItemAt(x, y);
  hovered_index_ = pointer_target_ >= 0 ? pointer_target_ : -1;
  pressed_index_ = -1;
  active_ = true;
  UpdateView();
  return true;
}

bool WindowMenu::HandleMotion(double x, double y) {
  if (!active_) return false;
  pointer_target_ = ItemAt(x, y);
  const int next = pointer_target_ >= 0 ? pointer_target_ : -1;
  if (next != hovered_index_) {
    hovered_index_ = next;
    UpdateView();
  }
  return true;
}

bool WindowMenu::HandleButton(uint32_t button, wl_pointer_button_state state) {
  if (!active_) return false;
  if (button != BTN_LEFT) {
    if (state == WL_POINTER_BUTTON_STATE_PRESSED) Cancel();
    return true;
  }
  if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
    if (hovered_index_ < 0) {
      if (pointer_target_ == -1) Cancel();
      return true;
    }
    pressed_index_ = hovered_index_;
    UpdateView();
    return true;
  }
  const int selected = pressed_index_ >= 0 && pressed_index_ == hovered_index_
                           ? pressed_index_
                           : -1;
  pressed_index_ = -1;
  if (selected >= 0) {
    Activate(selected);
  } else {
    UpdateView();
  }
  return true;
}

bool WindowMenu::HandleKey(wlr_keyboard* keyboard,
                           const wlr_keyboard_key_event& event) {
  if (!active_) return false;
  if (keyboard == nullptr || keyboard->xkb_state == nullptr) return true;
  const xkb_keysym_t symbol =
      xkb_state_key_get_one_sym(keyboard->xkb_state, event.keycode + 8);
  // Alt+F3 is registered by KeyBindingManager. Let its release reach the
  // manager so the no-repeat key bookkeeping is cleared while the menu owns
  // the rest of the keyboard grab.
  if (event.state == WL_KEYBOARD_KEY_STATE_RELEASED && symbol == XKB_KEY_F3) {
    return false;
  }
  if (event.state == WL_KEYBOARD_KEY_STATE_PRESSED) {
    if (symbol == XKB_KEY_Up) {
      MoveHighlight(-1);
    } else if (symbol == XKB_KEY_Down) {
      MoveHighlight(1);
    } else if (symbol == XKB_KEY_Alt_L || symbol == XKB_KEY_Alt_R) {
      Cancel();
    }
  } else if (event.state == WL_KEYBOARD_KEY_STATE_RELEASED &&
             (symbol == XKB_KEY_Return || symbol == XKB_KEY_KP_Enter ||
              symbol == XKB_KEY_Escape)) {
    if (symbol == XKB_KEY_Escape) {
      Cancel();
    } else {
      Activate(hovered_index_);
    }
  }
  return true;
}

void WindowMenu::SurfaceUnavailable(wlr_surface* surface) {
  if (surface != nullptr && surface == surface_) Cancel();
}

void WindowMenu::Cancel() {
  active_ = false;
  surface_ = nullptr;
  hovered_index_ = -1;
  pressed_index_ = -1;
  pointer_target_ = -1;
  if (tree_ != nullptr) wlr_scene_node_set_enabled(&tree_->node, false);
  ClearRegisteredBlur();
}

bool WindowMenu::IsActive() const { return active_; }

void WindowMenu::RebuildItems(const State& state) {
  items_ = {
      {Action::kMinimize, state.minimizable, false, false},
      {Action::kToggleMaximize, state.maximizable, false, false},
      {Action::kMove, state.movable, false, false},
      {Action::kResize, state.resizable, false, false},
      {Action::kToggleKeepAbove, true, state.kept_above, true},
      {Action::kToggleAllWorkspaces, true, state.all_workspaces, true},
      {Action::kMoveWorkspaceLeft, state.workspace > 0, false, false},
      {Action::kMoveWorkspaceRight, state.workspace + 1 < state.workspace_count,
       false, false},
      {Action::kClose, true, false, false},
  };

  QVariantList model;
  model.reserve(static_cast<qsizetype>(items_.size()));
  for (std::size_t index = 0; index < items_.size(); ++index) {
    const char* label = kLabels[index];
    if (index == 1 && state.maximized) label = "Restore";
    model.push_back(QVariantMap{
        {QStringLiteral("text"), QString::fromUtf8(label)},
        {QStringLiteral("enabled"), items_[index].enabled},
        {QStringLiteral("checked"), items_[index].checked},
        {QStringLiteral("checkable"), items_[index].checkable},
    });
  }
  renderer_->SetItems(model);
}

void WindowMenu::UpdateView() {
  if (!active_ || tree_ == nullptr || renderer_ == nullptr) return;
  renderer_->SetInteraction(hovered_index_, pressed_index_);
  if (renderer_->Render()) {
    wlr_scene_buffer_set_buffer_with_damage(node_, renderer_->Buffer(),
                                            nullptr);
  }
  wlr_scene_node_set_position(&blur_node_->node, x_, y_);
  wlr_scene_node_set_position(&node_->node, x_, y_);
  wlr_scene_node_set_enabled(&tree_->node, true);
  wlr_scene_node_raise_to_top(&tree_->node);
}

int WindowMenu::ItemAt(double x, double y) const {
  const double local_x = x - x_ - WindowMenuRenderer::kShadowMargin;
  const double panel_y = y - y_ - WindowMenuRenderer::kShadowMargin;
  if (local_x < 0 || local_x >= WindowMenuRenderer::kContentWidth ||
      panel_y < 0 || panel_y >= WindowMenuRenderer::kContentHeight) {
    return -1;
  }
  const double local_y = panel_y - WindowMenuRenderer::kContentMargin;
  if (local_y < 0 || local_y >= WindowMenuRenderer::kItemHeight *
                                    WindowMenuRenderer::kItemCount) {
    return -2;
  }
  const int index = static_cast<int>(local_y) / WindowMenuRenderer::kItemHeight;
  return index >= 0 && index < static_cast<int>(items_.size()) &&
                 items_[index].enabled
             ? index
             : -2;
}

void WindowMenu::MoveHighlight(int delta) {
  if (items_.empty()) return;
  int index = hovered_index_ < 0 ? (delta > 0 ? -1 : 0) : hovered_index_;
  for (std::size_t count = 0; count < items_.size(); ++count) {
    index = (index + delta + static_cast<int>(items_.size())) %
            static_cast<int>(items_.size());
    if (items_[index].enabled) {
      hovered_index_ = index;
      UpdateView();
      return;
    }
  }
}

void WindowMenu::Activate(int index) {
  if (index < 0 || index >= static_cast<int>(items_.size()) ||
      !items_[index].enabled || surface_ == nullptr) {
    return;
  }
  wlr_surface* surface = surface_;
  const Action action = items_[index].action;
  Cancel();
  if (action_handler_) action_handler_(surface, action);
}

bool WindowMenu::EnsureBlurBuffer() {
  if (blur_node_ == nullptr) return false;
  SsdBuffer* buffer = SsdBuffer::Create(WindowMenuRenderer::kWidth,
                                        WindowMenuRenderer::kHeight);
  if (buffer == nullptr) return false;
  buffer->Image().fill(Qt::transparent);
  wlr_scene_buffer_set_buffer(blur_node_, buffer->Handle());
  blur_buffer_ = buffer;
  return true;
}

void WindowMenu::ClearRegisteredBlur() {
  if (registered_blur_texture_ == nullptr) return;
  registered_blur_texture_ = nullptr;
  if (clear_blur_) clear_blur_(this);
}

void WindowMenu::OnBlurNodeSample(WindowMenu* menu,
                                  wlr_scene_output_sample_event*) {
  if (!menu->active_ || !menu->set_blur_ || menu->blur_node_ == nullptr ||
      menu->blur_buffer_ == nullptr) {
    return;
  }
  wlr_texture* texture = menu->blur_node_->WLR_PRIVATE.texture;
  if (texture == nullptr || texture == menu->registered_blur_texture_) return;

  constexpr int radius = 8;
  const int x = WindowMenuRenderer::kShadowMargin;
  const int y = WindowMenuRenderer::kShadowMargin;
  const int width = WindowMenuRenderer::kContentWidth;
  const int height = WindowMenuRenderer::kContentHeight;
  pixman_region32_t region;
  pixman_region32_init(&region);
  for (int row = 0; row < height; ++row) {
    int inset = 0;
    if (row < radius) {
      const double dy = static_cast<double>(radius - row) - 0.5;
      inset = static_cast<int>(
          std::ceil(radius - std::sqrt(radius * radius - dy * dy)));
    } else if (row >= height - radius) {
      const double dy = static_cast<double>(row - (height - radius)) + 0.5;
      inset = static_cast<int>(
          std::ceil(radius - std::sqrt(radius * radius - dy * dy)));
    }
    pixman_region32_union_rect(&region, &region, x + inset, y + row,
                               std::max(0, width - 2 * inset), 1);
  }
  if (menu->set_blur_(menu, texture, &region, kBlurOffset)) {
    menu->registered_blur_texture_ = texture;
  }
  pixman_region32_fini(&region);
}

}  // namespace view
}  // namespace flakewm
