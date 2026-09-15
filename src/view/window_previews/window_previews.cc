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

#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include <algorithm>
#include <cmath>
#include <ranges>
#include <utility>

#include "src/view/window_previews/window_previews_renderer.h"
#include "src/view/window_previews/window_previews.h"

namespace flakewm {
namespace view {
namespace {

constexpr int kFadeDurationMs = 150;
constexpr int kMoveDurationMs = 200;
constexpr int kTickMs = 10;
constexpr double kWallpaperDim = 0.42;
constexpr double kPanelDim = 0.60;
constexpr double kMinZoom = 1.05;
constexpr int kCloseSize = 32;
constexpr int kCloseInset = 10;

bool IgnoreInput(wlr_scene_buffer*, double*, double*) { return false; }

double Approach(double value, double target, double amount) {
  return value < target ? std::min(target, value + amount)
                        : std::max(target, value - amount);
}

double SmoothStep(double value) {
  const double t = std::clamp(value, 0.0, 1.0);
  return t * t * (3.0 - 2.0 * t);
}

WindowPreviewBox Interpolate(const WindowPreviewBox& from,
                             const WindowPreviewBox& to, double progress) {
  return {from.x + (to.x - from.x) * progress,
          from.y + (to.y - from.y) * progress,
          from.width + (to.width - from.width) * progress,
          from.height + (to.height - from.height) * progress};
}

bool Contains(const WindowPreviewBox& box, double x, double y) {
  return x >= box.x && x < box.x + box.width && y >= box.y &&
         y < box.y + box.height;
}

bool Overlaps(const WindowPreviewBox& a, const WindowPreviewBox& b) {
  return a.x < b.x + b.width && a.x + a.width > b.x && a.y < b.y + b.height &&
         a.y + a.height > b.y;
}

void SetRect(wlr_scene_rect* rect, int x, int y, int width, int height,
             float alpha) {
  if (rect == nullptr) return;
  wlr_scene_node_set_position(&rect->node, x, y);
  wlr_scene_rect_set_size(rect, std::max(1, width), std::max(1, height));
  const float color[4] = {0.0F, 0.0F, 0.0F, alpha};
  wlr_scene_rect_set_color(rect, color);
  wlr_scene_node_set_enabled(&rect->node, width > 0 && height > 0);
}

}  // namespace

WindowPreviews::WindowPreviews(
    wlr_scene_tree* overlay_parent, EntriesProvider entries_provider,
    CurrentWorkspace current_workspace, Activate activate, Close close,
    SetSourcesHidden set_sources_hidden, ScreenGeometry screen_geometry,
    UsableGeometry usable_geometry, CursorPosition cursor_position)
    : renderer_(std::make_unique<WindowPreviewsRenderer>()),
      entries_provider_(std::move(entries_provider)),
      current_workspace_(std::move(current_workspace)),
      activate_(std::move(activate)),
      close_(std::move(close)),
      set_sources_hidden_(std::move(set_sources_hidden)),
      screen_geometry_(std::move(screen_geometry)),
      usable_geometry_(std::move(usable_geometry)),
      cursor_position_(std::move(cursor_position)) {
  if (overlay_parent == nullptr) return;
  tree_ = wlr_scene_tree_create(overlay_parent);
  if (tree_ == nullptr) return;
  const float clear[4] = {0.0F, 0.0F, 0.0F, 0.0F};
  backdrop_ = wlr_scene_rect_create(tree_, 1, 1, clear);
  top_dim_ = wlr_scene_rect_create(tree_, 1, 1, clear);
  bottom_dim_ = wlr_scene_rect_create(tree_, 1, 1, clear);
  left_dim_ = wlr_scene_rect_create(tree_, 1, 1, clear);
  right_dim_ = wlr_scene_rect_create(tree_, 1, 1, clear);
  preview_tree_ = wlr_scene_tree_create(tree_);
  chrome_ = wlr_scene_buffer_create(tree_, nullptr);
  if (backdrop_ == nullptr || top_dim_ == nullptr || bottom_dim_ == nullptr ||
      left_dim_ == nullptr || right_dim_ == nullptr ||
      preview_tree_ == nullptr || chrome_ == nullptr) {
    wlr_scene_node_destroy(&tree_->node);
    tree_ = nullptr;
    return;
  }
  chrome_->point_accepts_input = IgnoreInput;
  animation_timer_.setInterval(kTickMs);
  QObject::connect(&animation_timer_, &QTimer::timeout,
                   [this]() { UpdateAnimation(); });
  wlr_scene_node_set_enabled(&tree_->node, false);
}

WindowPreviews::~WindowPreviews() {
  animation_timer_.stop();
  if (set_sources_hidden_) set_sources_hidden_(false);
  ClearPreviews();
  if (tree_ != nullptr) {
    wlr_scene_node_destroy(&tree_->node);
    tree_ = nullptr;
  }
}

bool WindowPreviews::Toggle() {
  if (active_ || closing_) {
    if (closing_) {
      active_ = true;
      closing_ = false;
      for (Item& item : items_) {
        item.from = item.current;
        item.to = item.target;
      }
      animation_clock_.restart();
      previous_tick_ms_ = 0;
      animation_timer_.start();
      UpdateView();
    } else {
      StartClose();
    }
    return true;
  }
  if (tree_ == nullptr || !entries_provider_ || !current_workspace_ ||
      !activate_ || !close_ || !screen_geometry_ || !usable_geometry_) {
    return false;
  }
  screen_ = screen_geometry_();
  usable_ = usable_geometry_();
  if (screen_.width <= 0 || screen_.height <= 0 || usable_.width <= 0 ||
      usable_.height <= 0 ||
      !renderer_->Resize(screen_.width, screen_.height)) {
    return false;
  }
  if (cursor_position_) {
    const auto [x, y] = cursor_position_();
    pointer_x_ = x - screen_.x;
    pointer_y_ = y - screen_.y;
  }
  filter_.clear();
  active_ = true;
  closing_ = false;
  Refresh();
  if (items_.empty()) {
    active_ = false;
    return false;
  }
  highlighted_ = ItemAt(pointer_x_, pointer_y_);
  decal_opacity_ = 0.0;
  backdrop_alpha_ = 0.0;
  panel_alpha_ = 0.0;
  if (set_sources_hidden_) set_sources_hidden_(true);
  wlr_scene_node_set_enabled(&tree_->node, true);
  wlr_scene_node_raise_to_top(&tree_->node);
  animation_clock_.restart();
  previous_tick_ms_ = 0;
  animation_timer_.start();
  UpdateView();
  return true;
}

bool WindowPreviews::HandleKey(wlr_keyboard* keyboard,
                               const wlr_keyboard_key_event& event) {
  if (!active_ && !closing_) return false;
  if (keyboard == nullptr || keyboard->xkb_state == nullptr) return true;
  if (event.state == WL_KEYBOARD_KEY_STATE_RELEASED) {
    if (last_key_ == event.keycode) key_released_ = true;
  }
  const xkb_keysym_t symbol =
      xkb_state_key_get_one_sym(keyboard->xkb_state, event.keycode + 8);
  const uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard);
  if ((modifiers & WLR_MODIFIER_LOGO) != 0 &&
      (symbol == XKB_KEY_a || symbol == XKB_KEY_A)) {
    return false;
  }
  if (event.state != WL_KEYBOARD_KEY_STATE_PRESSED || closing_) return true;

  const bool repeat = last_key_ == event.keycode && !key_released_;
  last_key_ = event.keycode;
  key_released_ = false;
  switch (symbol) {
    case XKB_KEY_Escape:
      StartClose();
      return true;
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
      ActivateItem(highlighted_);
      return true;
    case XKB_KEY_Left:
      highlighted_ = RelativeItem(highlighted_, -1, 0, !repeat);
      break;
    case XKB_KEY_Right:
      highlighted_ = RelativeItem(highlighted_, 1, 0, !repeat);
      break;
    case XKB_KEY_Up:
      highlighted_ = RelativeItem(highlighted_, 0, -1, !repeat);
      break;
    case XKB_KEY_Down:
      highlighted_ = RelativeItem(highlighted_, 0, 1, !repeat);
      break;
    case XKB_KEY_Home:
      highlighted_ = RelativeItem(highlighted_, -1000, 0, false);
      break;
    case XKB_KEY_End:
      highlighted_ = RelativeItem(highlighted_, 1000, 0, false);
      break;
    case XKB_KEY_Page_Up:
      highlighted_ = RelativeItem(highlighted_, 0, -1000, false);
      break;
    case XKB_KEY_Page_Down:
      highlighted_ = RelativeItem(highlighted_, 0, 1000, false);
      break;
    case XKB_KEY_BackSpace:
      if (!filter_.isEmpty()) {
        filter_.chop(filter_.back().isLowSurrogate() ? 2 : 1);
        highlighted_ = -1;
        Reflow();
      }
      return true;
    case XKB_KEY_Delete:
      if (!filter_.isEmpty()) {
        filter_.clear();
        highlighted_ = -1;
        Reflow();
      }
      return true;
    case XKB_KEY_Tab:
    case XKB_KEY_ISO_Left_Tab:
      return true;
    default: {
      if ((modifiers &
           (WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO)) == 0) {
        char buffer[16] = {};
        const int length = xkb_state_key_get_utf8(
            keyboard->xkb_state, event.keycode + 8, buffer, sizeof(buffer));
        if (length > 0) {
          filter_.append(QString::fromUtf8(buffer, length));
          highlighted_ = -1;
          Reflow();
        }
      }
      return true;
    }
  }
  UpdateView();
  return true;
}

bool WindowPreviews::HandleMotion(double layout_x, double layout_y) {
  if (!active_ && !closing_) return false;
  pointer_x_ = layout_x - screen_.x;
  pointer_y_ = layout_y - screen_.y;
  if (active_ && !closing_) UpdateHover();
  return true;
}

bool WindowPreviews::HandleButton(uint32_t button,
                                  wl_pointer_button_state state) {
  if (!active_ && !closing_) return false;
  if (state == WL_POINTER_BUTTON_STATE_PRESSED || closing_ ||
      (button != BTN_LEFT && button != BTN_RIGHT)) {
    return true;
  }
  const int hovered = ItemAt(pointer_x_, pointer_y_);
  if (hovered >= 0) {
    if (button == BTN_LEFT && CloseButtonAt(hovered, pointer_x_, pointer_y_)) {
      CloseItem(hovered);
    } else if (button == BTN_LEFT) {
      ActivateItem(hovered);
    } else {
      CloseItem(hovered);
    }
  } else if (button == BTN_LEFT) {
    StartClose();
  }
  return true;
}

void WindowPreviews::SurfaceUpdated(wlr_surface* surface) {
  if ((!active_ && !closing_) || surface == nullptr) return;
  if (std::ranges::any_of(items_, [surface](const Item& item) {
        return item.entry.surface == surface;
      })) {
    RebuildPreviews();
  }
}

void WindowPreviews::SurfaceUnavailable(wlr_surface* surface) {
  if ((!active_ && !closing_) || surface == nullptr || closing_) return;
  if (std::ranges::any_of(items_, [surface](const Item& item) {
        return item.entry.surface == surface;
      })) {
    Reflow(surface);
  }
}

void WindowPreviews::Cancel() {
  if (active_ && !closing_) StartClose();
}

bool WindowPreviews::IsActive() const { return active_ || closing_; }

void WindowPreviews::Refresh(wlr_surface* exclude) {
  std::vector<Entry> entries = entries_provider_();
  const WindowPreviewBox output = {
      static_cast<double>(screen_.x), static_cast<double>(screen_.y),
      static_cast<double>(screen_.width), static_cast<double>(screen_.height)};
  std::erase_if(entries, [&](const Entry& entry) {
    const WindowPreviewBox geometry = {
        static_cast<double>(entry.geometry.x),
        static_cast<double>(entry.geometry.y),
        static_cast<double>(entry.geometry.width),
        static_cast<double>(entry.geometry.height)};
    const bool matches = filter_.isEmpty() ||
                         entry.title.contains(filter_, Qt::CaseInsensitive) ||
                         entry.app_id.contains(filter_, Qt::CaseInsensitive);
    return entry.surface == nullptr || entry.surface == exclude ||
           entry.geometry.width <= 0 || entry.geometry.height <= 0 ||
           !Overlaps(geometry, output) || !matches;
  });
  std::stable_sort(
      entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        return a.geometry.y != b.geometry.y ? a.geometry.y < b.geometry.y
                                            : a.geometry.x < b.geometry.x;
      });

  std::vector<WindowPreviewBox> targets;
  targets.reserve(entries.size());
  for (const Entry& entry : entries) {
    targets.push_back({static_cast<double>(entry.geometry.x - screen_.x),
                       static_cast<double>(entry.geometry.y - screen_.y),
                       static_cast<double>(entry.geometry.width),
                       static_cast<double>(entry.geometry.height)});
  }
  CalculateNaturalLayout(&targets);
  items_.clear();
  items_.reserve(entries.size());
  const int workspace = current_workspace_();
  for (std::size_t index = 0; index < entries.size(); ++index) {
    const WindowPreviewBox original = {
        static_cast<double>(entries[index].geometry.x - screen_.x),
        static_cast<double>(entries[index].geometry.y - screen_.y),
        static_cast<double>(entries[index].geometry.width),
        static_cast<double>(entries[index].geometry.height)};
    items_.push_back({.entry = entries[index],
                      .original = original,
                      .target = targets[index],
                      .from = original,
                      .to = targets[index],
                      .current = original,
                      .opacity = entries[index].minimized ||
                                         entries[index].workspace != workspace
                                     ? 0.0
                                     : 1.0,
                      .highlight = 1.0});
  }
}

void WindowPreviews::Reflow(wlr_surface* exclude) {
  if (!active_ || closing_) return;
  wlr_surface* highlighted_surface =
      highlighted_ >= 0 && highlighted_ < static_cast<int>(items_.size())
          ? items_[highlighted_].entry.surface
          : nullptr;
  std::vector<std::pair<wlr_surface*, WindowPreviewBox>> previous;
  previous.reserve(items_.size());
  for (const Item& item : items_) {
    previous.emplace_back(item.entry.surface, item.current);
  }
  Refresh(exclude);
  if (items_.empty()) {
    StartClose();
    return;
  }
  for (Item& item : items_) {
    const auto old = std::find_if(
        previous.begin(), previous.end(),
        [&](const auto& value) { return value.first == item.entry.surface; });
    if (old != previous.end()) item.current = item.from = old->second;
  }
  if (!filter_.isEmpty()) {
    highlighted_ = 0;
  } else {
    const auto selected = std::find_if(
        items_.begin(), items_.end(), [highlighted_surface](const Item& item) {
          return item.entry.surface == highlighted_surface;
        });
    highlighted_ = selected == items_.end()
                       ? 0
                       : static_cast<int>(selected - items_.begin());
  }
  animation_clock_.restart();
  previous_tick_ms_ = 0;
  animation_timer_.start();
  UpdateView();
}

void WindowPreviews::UpdateView() {
  if ((!active_ && !closing_) || tree_ == nullptr || renderer_ == nullptr)
    return;
  UpdateVisuals();
  const int local_x = usable_.x - screen_.x;
  const int local_y = usable_.y - screen_.y;
  SetRect(backdrop_, local_x, local_y, usable_.width, usable_.height,
          static_cast<float>(backdrop_alpha_));
  SetRect(top_dim_, 0, 0, screen_.width, local_y,
          static_cast<float>(panel_alpha_));
  SetRect(bottom_dim_, 0, local_y + usable_.height, screen_.width,
          screen_.height - local_y - usable_.height,
          static_cast<float>(panel_alpha_));
  SetRect(left_dim_, 0, local_y, local_x, usable_.height,
          static_cast<float>(panel_alpha_));
  SetRect(right_dim_, local_x + usable_.width, local_y,
          screen_.width - local_x - usable_.width, usable_.height,
          static_cast<float>(panel_alpha_));

  std::vector<Entry> entries;
  std::vector<WindowPreviewVisual> visuals;
  entries.reserve(items_.size());
  visuals.reserve(items_.size());
  for (const Item& item : items_) {
    entries.push_back(item.entry);
    visuals.push_back(item.visual);
  }
  renderer_->SetState(entries, visuals, filter_, decal_opacity_);
  if (renderer_->Render()) {
    wlr_scene_buffer_set_buffer_with_damage(chrome_, renderer_->Buffer(),
                                            nullptr);
  }
  RebuildPreviews();
  wlr_scene_node_set_position(&tree_->node, screen_.x, screen_.y);
  wlr_scene_node_set_enabled(&tree_->node, true);
  wlr_scene_node_raise_to_top(&tree_->node);
}

void WindowPreviews::UpdateVisuals() {
  for (std::size_t index = 0; index < items_.size(); ++index) {
    Item& item = items_[index];
    double zoom = 1.0;
    if (!closing_ && item.highlight > 0.001 && item.current.width > 0.0 &&
        item.current.height > 0.0) {
      const double xr = screen_.width / item.current.width;
      const double yr = screen_.height / item.current.height;
      double cap = xr < yr ? std::max(xr / 4.0, yr / 32.0)
                           : std::max(xr / 32.0, yr / 4.0);
      cap = std::max(cap, kMinZoom);
      if (item.current.width * cap > screen_.width)
        cap = screen_.width / item.current.width;
      if (item.current.height * cap > screen_.height)
        cap = screen_.height / item.current.height;
      zoom += (cap - 1.0) * item.highlight;
    }
    const double width = item.current.width * zoom;
    const double height = item.current.height * zoom;
    double x = item.current.x - (width - item.current.width) / 2.0;
    double y = item.current.y - (height - item.current.height) / 2.0;
    const double clamped_x =
        std::max(x, 0.0) + std::min(0.0, screen_.width - (x + width));
    const double clamped_y =
        std::max(y, 0.0) + std::min(0.0, screen_.height - (y + height));
    x += (clamped_x - x) * item.highlight;
    y += (clamped_y - y) * item.highlight;
    const bool tiny = 2 * kCloseSize > item.target.width &&
                      2 * kCloseSize > item.target.height;
    item.visual = {
        .x = static_cast<int>(std::lround(x)),
        .y = static_cast<int>(std::lround(y)),
        .width = std::max(1, static_cast<int>(std::lround(width))),
        .height = std::max(1, static_cast<int>(std::lround(height))),
        .base_x = static_cast<int>(std::lround(item.current.x)),
        .base_y = static_cast<int>(std::lround(item.current.y)),
        .base_width =
            std::max(1, static_cast<int>(std::lround(item.current.width))),
        .base_height =
            std::max(1, static_cast<int>(std::lround(item.current.height))),
        .close_x =
            static_cast<int>(std::lround(x + width - kCloseSize - kCloseInset)),
        .close_y = static_cast<int>(std::lround(y + kCloseInset)),
        .opacity = item.opacity,
        .highlight = item.highlight,
        .show_close = !closing_ && static_cast<int>(index) == highlighted_ &&
                      !tiny && Contains(item.target, pointer_x_, pointer_y_)};
  }
}

void WindowPreviews::RebuildPreviews() {
  ClearPreviews();
  if (preview_tree_ == nullptr) return;
  for (const Item& item : items_) {
    wlr_surface* surface = item.entry.surface;
    if (surface == nullptr || surface->buffer == nullptr) continue;
    wlr_scene_buffer* preview =
        wlr_scene_buffer_create(preview_tree_, &surface->buffer->base);
    if (preview == nullptr) continue;
    wlr_scene_node_set_position(&preview->node, item.visual.x, item.visual.y);
    wlr_scene_buffer_set_dest_size(preview, item.visual.width,
                                   item.visual.height);
    wlr_scene_buffer_set_filter_mode(preview, WLR_SCALE_FILTER_BILINEAR);
    wlr_scene_buffer_set_opacity(preview, static_cast<float>(item.opacity));
    preview->point_accepts_input = IgnoreInput;
  }
}

void WindowPreviews::ClearPreviews() {
  if (preview_tree_ == nullptr) return;
  wlr_scene_node* node = nullptr;
  wlr_scene_node* temporary = nullptr;
  wl_list_for_each_safe(node, temporary, &preview_tree_->children, link) {
    wlr_scene_node_destroy(node);
  }
}

void WindowPreviews::StartClose() {
  if (!active_ || closing_) return;
  active_ = false;
  closing_ = true;
  for (Item& item : items_) {
    item.from = item.current;
    item.to = item.original;
  }
  animation_clock_.restart();
  previous_tick_ms_ = 0;
  animation_timer_.start();
  UpdateView();
}

void WindowPreviews::CompleteClose() {
  animation_timer_.stop();
  active_ = false;
  closing_ = false;
  ClearPreviews();
  if (tree_ != nullptr) wlr_scene_node_set_enabled(&tree_->node, false);
  if (set_sources_hidden_) set_sources_hidden_(false);
  items_.clear();
  filter_.clear();
  highlighted_ = -1;
}

void WindowPreviews::UpdateAnimation() {
  if (!active_ && !closing_) return;
  const qint64 elapsed = animation_clock_.elapsed();
  double dt = previous_tick_ms_ == 0 ? kTickMs : elapsed - previous_tick_ms_;
  previous_tick_ms_ = elapsed;
  dt = std::min(dt, 100.0);
  const double progress = SmoothStep(static_cast<double>(elapsed) /
                                     static_cast<double>(kMoveDurationMs));
  const double global = closing_ ? 0.0 : 1.0;
  decal_opacity_ = Approach(decal_opacity_, global, dt / kFadeDurationMs);
  backdrop_alpha_ =
      Approach(backdrop_alpha_, global * kWallpaperDim, dt / kFadeDurationMs);
  panel_alpha_ =
      Approach(panel_alpha_, global * kPanelDim, dt / kFadeDurationMs);
  for (std::size_t index = 0; index < items_.size(); ++index) {
    Item& item = items_[index];
    item.current = Interpolate(item.from, item.to, progress);
    item.opacity = Approach(item.opacity, 1.0, dt / kFadeDurationMs);
    const double highlight_target =
        !closing_ && static_cast<int>(index) == highlighted_ ? 1.0 : 0.0;
    item.highlight =
        Approach(item.highlight, highlight_target, dt / kFadeDurationMs);
  }
  UpdateView();
  if (closing_ && elapsed >= kMoveDurationMs + kTickMs) CompleteClose();
}

void WindowPreviews::ActivateItem(int index) {
  if (index < 0 || index >= static_cast<int>(items_.size())) {
    StartClose();
    return;
  }
  if (activate_) activate_(items_[index].entry.surface);
  StartClose();
}

void WindowPreviews::CloseItem(int index) {
  if (index < 0 || index >= static_cast<int>(items_.size())) return;
  wlr_surface* surface = items_[index].entry.surface;
  if (close_) close_(surface);
  if (highlighted_ == index) highlighted_ = -1;
  Reflow(surface);
}

void WindowPreviews::UpdateHover() {
  if (!filter_.isEmpty()) return;
  const int next = ItemAt(pointer_x_, pointer_y_);
  if (next == highlighted_) return;
  highlighted_ = next;
  animation_timer_.start();
  UpdateView();
}

int WindowPreviews::ItemAt(double x, double y) const {
  for (std::size_t index = 0; index < items_.size(); ++index) {
    if (Contains(items_[index].current, x, y)) return static_cast<int>(index);
  }
  return -1;
}

bool WindowPreviews::CloseButtonAt(int index, double x, double y) const {
  if (index < 0 || index >= static_cast<int>(items_.size())) return false;
  const WindowPreviewVisual& visual = items_[index].visual;
  return visual.show_close && x >= visual.close_x &&
         x < visual.close_x + kCloseSize && y >= visual.close_y &&
         y < visual.close_y + kCloseSize;
}

int WindowPreviews::RelativeItem(int index, int xdiff, int ydiff,
                                 bool wrap) const {
  if (items_.empty()) return -1;
  int current =
      index < 0 || index >= static_cast<int>(items_.size()) ? 0 : index;
  const int steps = xdiff != 0 ? std::abs(xdiff) : std::abs(ydiff);
  for (int step = 0; step < steps; ++step) {
    const WindowPreviewBox& window = items_[current].current;
    int next = -1;
    for (int candidate = 0; candidate < static_cast<int>(items_.size());
         ++candidate) {
      if (candidate == current) continue;
      const WindowPreviewBox& other = items_[candidate].current;
      WindowPreviewBox band = {};
      bool valid = false;
      if (xdiff > 0) {
        band = {0.0, window.y, static_cast<double>(screen_.width),
                window.height};
        valid = Overlaps(band, other) && other.x > window.x;
      } else if (xdiff < 0) {
        band = {0.0, window.y, static_cast<double>(screen_.width),
                window.height};
        valid = Overlaps(band, other) &&
                other.x + other.width < window.x + window.width;
      } else if (ydiff > 0) {
        band = {window.x, 0.0, window.width,
                static_cast<double>(screen_.height)};
        valid = Overlaps(band, other) && other.y > window.y;
      } else {
        band = {window.x, 0.0, window.width,
                static_cast<double>(screen_.height)};
        valid = Overlaps(band, other) &&
                other.y + other.height < window.y + window.height;
      }
      if (!valid) continue;
      if (next < 0) {
        next = candidate;
        continue;
      }
      const WindowPreviewBox& chosen = items_[next].current;
      const bool better =
          xdiff > 0   ? other.x < chosen.x
          : xdiff < 0 ? other.x + other.width > chosen.x + chosen.width
          : ydiff > 0 ? other.y < chosen.y
                      : other.y + other.height > chosen.y + chosen.height;
      if (better) next = candidate;
    }
    if (next < 0) {
      if (wrap) {
        return xdiff != 0
                   ? RelativeItem(current, xdiff > 0 ? -1000 : 1000, 0, false)
                   : RelativeItem(current, 0, ydiff > 0 ? -1000 : 1000, false);
      }
      break;
    }
    current = next;
  }
  return current;
}

}  // namespace view
}  // namespace flakewm
