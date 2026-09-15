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

#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include <algorithm>
#include <cmath>
#include <utility>

#include "src/view/multitasking/multitasking.h"
#include "src/view/multitasking/multitasking_renderer.h"
#include "src/view/multitasking/multitasking_wallpaper.h"
#include "src/view/ssd/ssd_buffer/ssd_buffer.h"

namespace flakewm {
namespace view {
namespace {

constexpr double kWorkspaceScale = 240.0 / 1920.0;
constexpr double kWorkspaceGapScale = 40.0 / 1920.0;
constexpr double kVerticalSpacingScale = 20.0 / 1080.0;
constexpr int kMaximumDesktopCount = 6;
constexpr int kAnimationDurationMs = 300;
constexpr int kAnimationTickMs = 10;
constexpr float kBackdropColor[4] = {0.0F, 0.0F, 0.0F, 0.6F};

bool IgnoreInput(wlr_scene_buffer*, double*, double*) { return false; }

double OutQuint(double progress) {
  const double remaining = 1.0 - std::clamp(progress, 0.0, 1.0);
  return 1.0 - remaining * remaining * remaining * remaining * remaining;
}

MultitaskingWindowPlacement Interpolate(const MultitaskingWindowPlacement& from,
                                        const MultitaskingWindowPlacement& to,
                                        double progress) {
  return {
      .x = static_cast<int>(std::lround(from.x + (to.x - from.x) * progress)),
      .y = static_cast<int>(std::lround(from.y + (to.y - from.y) * progress)),
      .width = std::max(
          1, static_cast<int>(
                 std::lround(from.width + (to.width - from.width) * progress))),
      .height =
          std::max(1, static_cast<int>(std::lround(
                          from.height + (to.height - from.height) * progress))),
  };
}

}  // namespace

Multitasking::Multitasking(
    wlr_scene_tree* overlay_parent, EntriesProvider entries_provider,
    WorkspaceCount workspace_count, CurrentWorkspace current_workspace,
    SwitchWorkspace switch_workspace, Activate activate, Close close,
    ToggleKeptAbove toggle_kept_above, AddWorkspace add_workspace,
    RemoveWorkspace remove_workspace, ReorderWorkspace reorder_workspace,
    MoveToWorkspace move_to_workspace, SetSourcesHidden set_sources_hidden,
    ScreenGeometry screen_geometry)
    : renderer_(std::make_unique<MultitaskingRenderer>()),
      entries_provider_(std::move(entries_provider)),
      workspace_count_(std::move(workspace_count)),
      current_workspace_(std::move(current_workspace)),
      switch_workspace_(std::move(switch_workspace)),
      activate_(std::move(activate)),
      close_(std::move(close)),
      toggle_kept_above_(std::move(toggle_kept_above)),
      add_workspace_(std::move(add_workspace)),
      remove_workspace_(std::move(remove_workspace)),
      reorder_workspace_(std::move(reorder_workspace)),
      move_to_workspace_(std::move(move_to_workspace)),
      set_sources_hidden_(std::move(set_sources_hidden)),
      screen_geometry_(std::move(screen_geometry)) {
  if (overlay_parent == nullptr) return;
  tree_ = wlr_scene_tree_create(overlay_parent);
  if (tree_ == nullptr) return;
  background_node_ = wlr_scene_buffer_create(tree_, nullptr);
  backdrop_node_ = wlr_scene_rect_create(tree_, 1, 1, kBackdropColor);
  preview_tree_ = wlr_scene_tree_create(tree_);
  node_ = wlr_scene_buffer_create(tree_, nullptr);
  if (background_node_ == nullptr || backdrop_node_ == nullptr ||
      preview_tree_ == nullptr || node_ == nullptr) {
    wlr_scene_node_destroy(&tree_->node);
    tree_ = nullptr;
    background_node_ = nullptr;
    backdrop_node_ = nullptr;
    preview_tree_ = nullptr;
    node_ = nullptr;
    return;
  }
  background_node_->point_accepts_input = IgnoreInput;
  node_->point_accepts_input = IgnoreInput;
  animation_timer_.setInterval(kAnimationTickMs);
  QObject::connect(&animation_timer_, &QTimer::timeout,
                   [this]() { UpdateAnimation(); });
  wlr_scene_node_set_enabled(&tree_->node, false);
}

Multitasking::~Multitasking() {
  animation_timer_.stop();
  if (set_sources_hidden_) set_sources_hidden_(false);
  ClearPreviews();
  if (tree_ != nullptr) {
    wlr_scene_node_destroy(&tree_->node);
    tree_ = nullptr;
  }
  if (background_buffer_ != nullptr) {
    wlr_buffer_drop(background_buffer_->Handle());
    background_buffer_ = nullptr;
  }
  if (workspace_wallpaper_buffer_ != nullptr) {
    wlr_buffer_drop(workspace_wallpaper_buffer_->Handle());
    workspace_wallpaper_buffer_ = nullptr;
  }
}

bool Multitasking::Toggle() {
  if (active_ || closing_) {
    if (!closing_) StartAnimation(true);
    return true;
  }
  if (!entries_provider_ || !workspace_count_ || !current_workspace_ ||
      !switch_workspace_ || !activate_ || !screen_geometry_) {
    return false;
  }
  const int count = workspace_count_();
  if (count <= 0) return false;
  active_ = true;
  closing_ = false;
  pending_activation_ = nullptr;
  pending_close_ = nullptr;
  pending_workspace_ = -1;
  selected_workspace_ = std::clamp(current_workspace_(), 0, count - 1);
  hovered_workspace_ = -1;
  hovered_window_ = -1;
  hovered_control_ = Control::kNone;
  wlr_scene_buffer_set_buffer(background_node_, nullptr);
  if (background_buffer_ != nullptr) {
    wlr_buffer_drop(background_buffer_->Handle());
    background_buffer_ = nullptr;
  }
  if (workspace_wallpaper_buffer_ != nullptr) {
    wlr_buffer_drop(workspace_wallpaper_buffer_->Handle());
    workspace_wallpaper_buffer_ = nullptr;
  }
  Refresh();
  UpdateView();
  if (!active_) return false;
  if (set_sources_hidden_) set_sources_hidden_(true);
  animation_to_ = window_placements_;
  animation_from_.clear();
  animation_from_.reserve(visible_entries_.size());
  for (const Entry& entry : visible_entries_) {
    animation_from_.push_back({
        .x = entry.geometry.x - screen_.x,
        .y = entry.geometry.y - screen_.y,
        .width = std::max(1, entry.geometry.width),
        .height = std::max(1, entry.geometry.height),
    });
  }
  window_placements_ = animation_from_;
  workspace_opacity_ = 0.0;
  StartAnimation(false);
  return true;
}

bool Multitasking::HandleKey(wlr_keyboard* keyboard,
                             const wlr_keyboard_key_event& event) {
  if (!active_ || closing_) return active_ || closing_;
  if (keyboard == nullptr || keyboard->xkb_state == nullptr) return true;
  const xkb_keysym_t symbol =
      xkb_state_key_get_one_sym(keyboard->xkb_state, event.keycode + 8);
  const bool logo =
      (wlr_keyboard_get_modifiers(keyboard) & WLR_MODIFIER_LOGO) != 0;

  // Leave the registered shortcuts to KeyBindingManager so a second Super+S
  // or Super+Tab closes the overview with balanced shortcut state.
  if (logo && (symbol == XKB_KEY_s || symbol == XKB_KEY_S ||
               symbol == XKB_KEY_Tab || symbol == XKB_KEY_ISO_Left_Tab)) {
    return false;
  }
  if (event.state == WL_KEYBOARD_KEY_STATE_PRESSED &&
      (symbol == XKB_KEY_Escape || symbol == XKB_KEY_s || symbol == XKB_KEY_S ||
       symbol == XKB_KEY_Tab || symbol == XKB_KEY_ISO_Left_Tab)) {
    StartAnimation(true);
  }
  return true;
}

bool Multitasking::HandleMotion(double layout_x, double layout_y) {
  if (!active_ || closing_) return active_ || closing_;
  pointer_x_ = layout_x - screen_.x;
  pointer_y_ = layout_y - screen_.y;
  if (pressed_control_ == Control::kWindow ||
      pressed_control_ == Control::kWorkspace) {
    const double dx = pointer_x_ - press_x_;
    const double dy = pointer_y_ - press_y_;
    if (!dragging_ && dx * dx + dy * dy > 100.0) dragging_ = true;
  }
  if (dragging_ && pressed_control_ == Control::kWindow &&
      pressed_window_ >= 0 &&
      pressed_window_ < static_cast<int>(window_placements_.size())) {
    MultitaskingWindowPlacement& placement =
        window_placements_[static_cast<std::size_t>(pressed_window_)];
    placement.x =
        static_cast<int>(std::lround(pointer_x_ - placement.width / 2.0));
    placement.y =
        static_cast<int>(std::lround(pointer_y_ - placement.height / 2.0));
    UpdateView();
    return true;
  }
  if (dragging_ && pressed_control_ == Control::kWorkspace) {
    dragged_workspace_ = pressed_workspace_;
    UpdateView();
    return true;
  }

  const int next_workspace = WorkspaceAt(pointer_x_, pointer_y_);
  const int next_window = WindowAt(pointer_x_, pointer_y_);
  Control next_control = Control::kNone;
  if (next_window >= 0) {
    next_control = PointerOnCloseButton(next_window) ? Control::kWindowClose
                   : PointerOnTopButton(next_window) ? Control::kWindowTop
                                                     : Control::kWindow;
  } else if (PointerOnAddButton()) {
    next_control = Control::kAddWorkspace;
  } else if (next_workspace >= 0) {
    next_control = PointerOnWorkspaceCloseButton(next_workspace)
                       ? Control::kWorkspaceClose
                       : Control::kWorkspace;
  }
  if (hovered_workspace_ != next_workspace || hovered_window_ != next_window ||
      hovered_control_ != next_control) {
    hovered_workspace_ = next_workspace;
    hovered_window_ = next_window;
    hovered_control_ = next_control;
    UpdateView();
  }
  return true;
}

bool Multitasking::HandleButton(uint32_t button,
                                wl_pointer_button_state state) {
  if (!active_ || closing_) return active_ || closing_;
  if (button != BTN_LEFT) return true;
  if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
    pressed_control_ = hovered_control_;
    pressed_workspace_ = hovered_workspace_;
    pressed_window_ = hovered_window_;
    press_x_ = pointer_x_;
    press_y_ = pointer_y_;
    dragging_ = false;
    return true;
  }

  if (dragging_ && pressed_control_ == Control::kWindow &&
      pressed_window_ >= 0 &&
      pressed_window_ < static_cast<int>(visible_entries_.size())) {
    const int workspace = WorkspaceAt(pointer_x_, pointer_y_);
    if (workspace >= 0 && move_to_workspace_) {
      move_to_workspace_(visible_entries_[pressed_window_].surface, workspace);
      Refresh();
    }
    dragging_ = false;
    pressed_control_ = Control::kNone;
    CalculateWindowLayout();
    UpdateView();
    return true;
  }
  if (dragging_ && pressed_control_ == Control::kWorkspace) {
    const int target = WorkspaceAt(pointer_x_, pointer_y_);
    if (target >= 0 && target != pressed_workspace_ && reorder_workspace_) {
      reorder_workspace_(pressed_workspace_, target);
      selected_workspace_ = target;
      Refresh();
    }
    dragging_ = false;
    dragged_workspace_ = -1;
    pressed_control_ = Control::kNone;
    UpdateView();
    return true;
  }

  const Control released = hovered_control_;
  if (released == pressed_control_) {
    if (released == Control::kWindowClose && pressed_window_ >= 0 &&
        pressed_window_ < static_cast<int>(visible_entries_.size())) {
      pending_close_ = visible_entries_[pressed_window_].surface;
      StartAnimation(true);
    } else if (released == Control::kWindowTop && pressed_window_ >= 0 &&
               pressed_window_ < static_cast<int>(visible_entries_.size()) &&
               toggle_kept_above_) {
      toggle_kept_above_(visible_entries_[pressed_window_].surface);
      Refresh();
      UpdateView();
    } else if (released == Control::kWindow && pressed_window_ >= 0 &&
               pressed_window_ < static_cast<int>(visible_entries_.size())) {
      pending_activation_ = visible_entries_[pressed_window_].surface;
      StartAnimation(true);
    } else if (released == Control::kWorkspace && pressed_workspace_ >= 0) {
      pending_workspace_ = pressed_workspace_;
      StartAnimation(true);
    } else if (released == Control::kWorkspaceClose &&
               pressed_workspace_ >= 0 && remove_workspace_ &&
               remove_workspace_(pressed_workspace_)) {
      selected_workspace_ = std::clamp(selected_workspace_, 0,
                                       std::max(0, workspace_count_() - 1));
      Refresh();
      UpdateView();
    } else if (released == Control::kAddWorkspace && add_workspace_ &&
               workspace_count_() < kMaximumDesktopCount && add_workspace_()) {
      Refresh();
      UpdateView();
    } else if (released == Control::kNone) {
      StartAnimation(true);
    }
  }
  pressed_control_ = Control::kNone;
  pressed_workspace_ = -1;
  pressed_window_ = -1;
  return true;
}

void Multitasking::SurfaceUpdated(wlr_surface* surface) {
  if ((!active_ && !closing_) || surface == nullptr) return;
  const auto matches = [surface](const Entry& entry) {
    return entry.surface == surface;
  };
  if (std::ranges::any_of(entries_, matches)) RebuildPreviews();
}

void Multitasking::SurfaceUnavailable(wlr_surface* surface) {
  if ((!active_ && !closing_) || surface == nullptr) return;
  if (pending_activation_ == surface) pending_activation_ = nullptr;
  if (pending_close_ == surface) pending_close_ = nullptr;
  Refresh();
  UpdateView();
}

void Multitasking::Cancel() {
  if (active_ && !closing_) StartAnimation(true);
}

bool Multitasking::IsActive() const { return active_ || closing_; }

void Multitasking::Refresh() {
  entries_ = entries_provider_();
  const int count = std::max(1, workspace_count_());
  std::erase_if(entries_, [count](const Entry& entry) {
    return entry.surface == nullptr || entry.workspace < 0 ||
           entry.workspace >= count;
  });
  selected_workspace_ = std::clamp(selected_workspace_, 0, count - 1);
  workspace_window_counts_.assign(static_cast<std::size_t>(count), 0);
  for (const Entry& entry : entries_) {
    ++workspace_window_counts_[static_cast<std::size_t>(entry.workspace)];
  }
  visible_entries_.clear();
  for (auto iterator = entries_.rbegin(); iterator != entries_.rend();
       ++iterator) {
    if (iterator->workspace == selected_workspace_) {
      visible_entries_.push_back(*iterator);
    }
  }
  const auto focused =
      std::find_if(visible_entries_.begin(), visible_entries_.end(),
                   [](const Entry& entry) { return entry.active; });
  selected_window_ = focused == visible_entries_.end()
                         ? (visible_entries_.empty() ? -1 : 0)
                         : static_cast<int>(focused - visible_entries_.begin());
  hovered_window_ = -1;
}

void Multitasking::UpdateView() {
  if ((!active_ && !closing_) || tree_ == nullptr || node_ == nullptr ||
      renderer_ == nullptr) {
    if (tree_ != nullptr) wlr_scene_node_set_enabled(&tree_->node, false);
    return;
  }
  screen_ = screen_geometry_();
  if (screen_.width <= 0 || screen_.height <= 0 ||
      !renderer_->Resize(screen_.width, screen_.height)) {
    CompleteClose();
    return;
  }

  if (background_buffer_ == nullptr ||
      background_buffer_->Handle()->width != screen_.width ||
      background_buffer_->Handle()->height != screen_.height) {
    SsdBuffer* wallpaper =
        CreateMultitaskingWallpaper(screen_.width, screen_.height);
    if (wallpaper != nullptr) {
      wlr_scene_buffer_set_buffer(background_node_, wallpaper->Handle());
      if (background_buffer_ != nullptr) {
        wlr_buffer_drop(background_buffer_->Handle());
      }
      background_buffer_ = wallpaper;
    }
  }
  wlr_scene_rect_set_size(backdrop_node_, screen_.width, screen_.height);
  const int workspace_count =
      std::max(1, static_cast<int>(workspace_window_counts_.size()));
  layout_.width = screen_.width;
  layout_.height = screen_.height;
  layout_.workspace_y = std::max(
      1, static_cast<int>(std::lround(screen_.height * kVerticalSpacingScale)));
  layout_.workspace_width = std::max(
      1, static_cast<int>(std::lround(screen_.width * kWorkspaceScale)));
  layout_.workspace_height = std::max(
      1, static_cast<int>(std::lround(screen_.height * kWorkspaceScale)));
  layout_.workspace_gap = std::max(
      1, static_cast<int>(std::lround(screen_.width * kWorkspaceGapScale)));
  const int total_width = workspace_count * layout_.workspace_width +
                          (workspace_count - 1) * layout_.workspace_gap;
  layout_.workspace_x = (screen_.width - total_width) / 2;
  layout_.window_area_y = layout_.workspace_y * 2 + layout_.workspace_height;
  layout_.add_x = screen_.width - 104;
  layout_.add_y =
      layout_.workspace_y + (layout_.workspace_height - layout_.add_size) / 2;
  layout_.dragged_workspace = dragging_ ? dragged_workspace_ : -1;
  layout_.dragged_workspace_x =
      static_cast<int>(std::lround(pointer_x_ - layout_.workspace_width / 2.0));
  if (workspace_wallpaper_buffer_ == nullptr && background_buffer_ != nullptr) {
    workspace_wallpaper_buffer_ = CreateMultitaskingWorkspaceWallpaper(
        background_buffer_, layout_.workspace_width - 2,
        layout_.workspace_height - 2);
  }
  if (!animating_ && !dragging_) CalculateWindowLayout();

  renderer_->SetState(visible_entries_, workspace_window_counts_,
                      window_placements_,
                      std::clamp(current_workspace_(), 0, workspace_count - 1),
                      selected_workspace_, selected_window_, hovered_workspace_,
                      hovered_window_, workspace_opacity_, layout_);
  if (renderer_->Render()) {
    wlr_scene_buffer_set_buffer_with_damage(node_, renderer_->Buffer(),
                                            nullptr);
  }
  RebuildPreviews();
  wlr_scene_node_set_position(&background_node_->node, screen_.x, screen_.y);
  wlr_scene_node_set_position(&backdrop_node_->node, screen_.x, screen_.y);
  wlr_scene_node_set_position(&node_->node, screen_.x, screen_.y);
  wlr_scene_node_set_enabled(&tree_->node, true);
  wlr_scene_node_raise_to_top(&tree_->node);
}

void Multitasking::StartAnimation(bool closing) {
  if (!active_ || (closing_ && closing)) return;
  closing_ = closing;
  animating_ = true;
  animation_from_ = window_placements_;
  animation_to_.clear();
  if (closing) {
    animation_to_.reserve(visible_entries_.size());
    for (const Entry& entry : visible_entries_) {
      animation_to_.push_back({
          .x = entry.geometry.x - screen_.x,
          .y = entry.geometry.y - screen_.y,
          .width = std::max(1, entry.geometry.width),
          .height = std::max(1, entry.geometry.height),
      });
    }
  } else if (animation_to_.empty()) {
    CalculateWindowLayout();
    animation_to_ = window_placements_;
    window_placements_ = animation_from_;
  }
  animation_clock_.restart();
  animation_timer_.start();
  UpdateView();
}

void Multitasking::UpdateAnimation() {
  if (!animating_) return;
  const double raw =
      static_cast<double>(animation_clock_.elapsed()) / kAnimationDurationMs;
  const double progress = OutQuint(raw);
  const std::size_t count =
      std::min(animation_from_.size(), animation_to_.size());
  window_placements_.resize(count);
  for (std::size_t index = 0; index < count; ++index) {
    window_placements_[index] =
        Interpolate(animation_from_[index], animation_to_[index], progress);
  }
  if (!closing_) workspace_opacity_ = std::clamp(raw, 0.0, 1.0);
  if (raw >= 1.0) {
    animation_timer_.stop();
    animating_ = false;
    window_placements_ = animation_to_;
    if (closing_) {
      CompleteClose();
      return;
    }
    workspace_opacity_ = 1.0;
  }
  UpdateView();
}

void Multitasking::CompleteClose() {
  animation_timer_.stop();
  const wlr_surface* activation = pending_activation_;
  wlr_surface* close_surface = pending_close_;
  const int workspace = pending_workspace_;
  active_ = false;
  closing_ = false;
  animating_ = false;
  dragging_ = false;
  pending_activation_ = nullptr;
  pending_close_ = nullptr;
  pending_workspace_ = -1;
  ClearPreviews();
  if (tree_ != nullptr) wlr_scene_node_set_enabled(&tree_->node, false);
  if (set_sources_hidden_) set_sources_hidden_(false);
  if (close_surface != nullptr && close_) close_(close_surface);
  if (activation != nullptr && activate_) {
    activate_(const_cast<wlr_surface*>(activation));
  } else if (workspace >= 0 && switch_workspace_) {
    switch_workspace_(workspace);
  }
  entries_.clear();
  visible_entries_.clear();
}

void Multitasking::Finish(bool activate_window) {
  if (activate_window && selected_window_ >= 0 &&
      selected_window_ < static_cast<int>(visible_entries_.size())) {
    pending_activation_ = visible_entries_[selected_window_].surface;
  }
  StartAnimation(true);
}

void Multitasking::RebuildPreviews() {
  ClearPreviews();
  if ((!active_ && !closing_) || preview_tree_ == nullptr) return;
  const double scale_x = static_cast<double>(layout_.workspace_width - 2) /
                         std::max(1, screen_.width);
  const double scale_y = static_cast<double>(layout_.workspace_height - 2) /
                         std::max(1, screen_.height);

  if (workspace_wallpaper_buffer_ != nullptr) {
    for (int workspace = 0;
         workspace < static_cast<int>(workspace_window_counts_.size());
         ++workspace) {
      wlr_scene_buffer* wallpaper = wlr_scene_buffer_create(
          preview_tree_, workspace_wallpaper_buffer_->Handle());
      if (wallpaper == nullptr) continue;
      wlr_scene_node_set_position(&wallpaper->node,
                                  screen_.x + WorkspaceX(workspace) + 1,
                                  screen_.y + layout_.workspace_y + 1);
      wlr_scene_buffer_set_dest_size(wallpaper, layout_.workspace_width - 2,
                                     layout_.workspace_height - 2);
      wlr_scene_buffer_set_filter_mode(wallpaper, WLR_SCALE_FILTER_BILINEAR);
      wlr_scene_buffer_set_opacity(wallpaper,
                                   static_cast<float>(workspace_opacity_));
      wallpaper->point_accepts_input = IgnoreInput;
    }
  }

  for (const Entry& entry : entries_) {
    if (entry.surface == nullptr || entry.surface->buffer == nullptr) continue;
    wlr_scene_buffer* preview =
        wlr_scene_buffer_create(preview_tree_, &entry.surface->buffer->base);
    if (preview == nullptr) continue;
    const int x =
        WorkspaceX(entry.workspace) + 1 +
        static_cast<int>(std::lround((entry.geometry.x - screen_.x) * scale_x));
    const int y =
        layout_.workspace_y + 1 +
        static_cast<int>(std::lround((entry.geometry.y - screen_.y) * scale_y));
    wlr_scene_node_set_position(&preview->node, screen_.x + x, screen_.y + y);
    wlr_scene_buffer_set_dest_size(
        preview,
        std::max(1,
                 static_cast<int>(std::lround(entry.geometry.width * scale_x))),
        std::max(
            1, static_cast<int>(std::lround(entry.geometry.height * scale_y))));
    wlr_scene_buffer_set_filter_mode(preview, WLR_SCALE_FILTER_BILINEAR);
    wlr_scene_buffer_set_opacity(preview,
                                 static_cast<float>(workspace_opacity_));
    preview->point_accepts_input = IgnoreInput;
  }

  for (int index = 0; index < static_cast<int>(visible_entries_.size());
       ++index) {
    if (index >= static_cast<int>(window_placements_.size())) break;
    wlr_surface* surface = visible_entries_[index].surface;
    if (surface == nullptr || surface->buffer == nullptr) continue;
    wlr_scene_buffer* preview =
        wlr_scene_buffer_create(preview_tree_, &surface->buffer->base);
    if (preview == nullptr) continue;
    const MultitaskingWindowPlacement& placement = window_placements_[index];
    wlr_scene_node_set_position(&preview->node, screen_.x + placement.x,
                                screen_.y + placement.y);
    wlr_scene_buffer_set_dest_size(preview, placement.width, placement.height);
    wlr_scene_buffer_set_filter_mode(preview, WLR_SCALE_FILTER_BILINEAR);
    preview->point_accepts_input = IgnoreInput;
  }
}

void Multitasking::ClearPreviews() {
  if (preview_tree_ == nullptr) return;
  wlr_scene_node* node = nullptr;
  wlr_scene_node* temporary = nullptr;
  wl_list_for_each_safe(node, temporary, &preview_tree_->children, link) {
    wlr_scene_node_destroy(node);
  }
}

void Multitasking::SelectWorkspace(int workspace) {
  const int count = static_cast<int>(workspace_window_counts_.size());
  if (workspace < 0 || workspace >= count || workspace == selected_workspace_) {
    return;
  }
  selected_workspace_ = workspace;
  Refresh();
  UpdateView();
}

void Multitasking::StepWindow(int delta) {
  const int count = static_cast<int>(visible_entries_.size());
  if (count <= 0) return;
  selected_window_ = (std::max(0, selected_window_) + delta) % count;
  if (selected_window_ < 0) selected_window_ += count;
  UpdateView();
}

int Multitasking::WorkspaceX(int index) const {
  if (dragging_ && dragged_workspace_ == index) {
    return static_cast<int>(
        std::lround(pointer_x_ - layout_.workspace_width / 2.0));
  }
  return layout_.workspace_x +
         index * (layout_.workspace_width + layout_.workspace_gap);
}

bool Multitasking::PointerOnCloseButton(int index) const {
  if (index < 0 || index >= static_cast<int>(window_placements_.size()))
    return false;
  const auto& placement = window_placements_[index];
  return pointer_x_ >= placement.x + placement.width - 25 &&
         pointer_x_ < placement.x + placement.width + 23 &&
         pointer_y_ >= placement.y - 17 && pointer_y_ < placement.y + 31;
}

bool Multitasking::PointerOnTopButton(int index) const {
  if (index < 0 || index >= static_cast<int>(window_placements_.size()))
    return false;
  const auto& placement = window_placements_[index];
  return pointer_x_ >= placement.x - 22 && pointer_x_ < placement.x + 26 &&
         pointer_y_ >= placement.y - 17 && pointer_y_ < placement.y + 31;
}

bool Multitasking::PointerOnWorkspaceCloseButton(int index) const {
  if (workspace_window_counts_.size() <= 1 || index < 0 ||
      index >= static_cast<int>(workspace_window_counts_.size()))
    return false;
  const int x = WorkspaceX(index) + layout_.workspace_width - 30;
  return pointer_x_ >= x && pointer_x_ < x + 48 &&
         pointer_y_ >= layout_.workspace_y - 13 &&
         pointer_y_ < layout_.workspace_y + 35;
}

bool Multitasking::PointerOnAddButton() const {
  return workspace_window_counts_.size() < kMaximumDesktopCount &&
         pointer_x_ >= layout_.add_x &&
         pointer_x_ < layout_.add_x + layout_.add_size &&
         pointer_y_ >= layout_.add_y &&
         pointer_y_ < layout_.add_y + layout_.add_size;
}

int Multitasking::WorkspaceAt(double local_x, double local_y) const {
  if (local_y < layout_.workspace_y ||
      local_y >= layout_.workspace_y + layout_.workspace_height)
    return -1;
  for (int index = 0; index < static_cast<int>(workspace_window_counts_.size());
       ++index) {
    const int x = WorkspaceX(index);
    if (local_x >= x && local_x < x + layout_.workspace_width) return index;
  }
  return -1;
}

int Multitasking::WindowAt(double local_x, double local_y) const {
  for (int index = static_cast<int>(window_placements_.size()) - 1; index >= 0;
       --index) {
    const auto& placement = window_placements_[index];
    if (local_x >= placement.x && local_x < placement.x + placement.width &&
        local_y >= placement.y && local_y < placement.y + placement.height) {
      return index;
    }
  }
  return -1;
}

}  // namespace view
}  // namespace flakewm
