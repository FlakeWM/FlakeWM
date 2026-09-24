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
 * Originally copyright by (C) 2024-2026 UnionTech Software Technology Co., Ltd.
 * Original license: Apache-2.0 OR LGPL-3.0-only OR GPL-2.0-only OR
 *                   GPL-3.0-only.
 * Redistributed with GPL-3.0-only.
 * Original code is modified to adapt C++ and Wlroots 0.20.2.
 */

#include "src/view/window_selecter/window_selector.h"

#include <absl/log/absl_log.h>
#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon-keysyms.h>

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
#include <cmath>
#include <memory>
#include <utility>

#include "src/view/ssd/ssd_buffer/ssd_buffer.h"

namespace flakewm {
namespace view {
namespace {

constexpr int kBorderWidth = 2;
constexpr int kDashLength = 8;
constexpr int kDashGap = 6;
constexpr double kDragThreshold = 2.0;
constexpr int kToolbarButtonSize = 50;
constexpr int kToolbarHorizontalPadding = 6;
constexpr int kToolbarHeight = 58;
constexpr int kToolbarBottomMargin = 10;
constexpr std::array<float, 4> kDimColor = {0.0F, 0.0F, 0.0F, 0.3F};
constexpr std::array<float, 4> kBorderColor = {1.0F, 1.0F, 1.0F, 1.0F};

bool IgnoreInput(wlr_scene_buffer*, double*, double*) { return false; }

QQmlEngine* ToolbarEngine() {
  static auto* engine = new QQmlEngine(QCoreApplication::instance());
  return engine;
}

std::vector<WindowSelector::Mode> AvailableModes(uint32_t mask) {
  std::vector<WindowSelector::Mode> modes;
  for (WindowSelector::Mode mode :
       {WindowSelector::Mode::kRegion, WindowSelector::Mode::kWindow,
        WindowSelector::Mode::kOutput}) {
    if ((mask & WindowSelector::ModeMask(mode)) != 0) modes.push_back(mode);
  }
  return modes;
}

bool ValidTarget(const std::optional<WindowSelector::Target>& target) {
  return target.has_value() && target->output != nullptr &&
         target->box.width > 0 && target->box.height > 0;
}

}  // namespace

class WindowSelectorToolbarRenderer final {
 public:
  WindowSelectorToolbarRenderer()
      : render_control_(std::make_unique<QQuickRenderControl>()),
        window_(std::make_unique<QQuickWindow>(render_control_.get())) {
    QQmlComponent component(ToolbarEngine(),
                            QUrl("qrc:/flakewm/window_selector_toolbar.qml"));
    if (component.isError()) {
      ABSL_LOG(ERROR) << "Failed to load window selector toolbar QML: "
                      << component.errorString().toStdString();
      return;
    }
    QObject* object = component.create();
    root_item_ = qobject_cast<QQuickItem*>(object);
    if (root_item_ == nullptr) {
      ABSL_LOG(ERROR) << "Window selector toolbar root is not a QQuickItem";
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

  ~WindowSelectorToolbarRenderer() {
    QObject::disconnect(render_requested_);
    QObject::disconnect(scene_changed_);
    if (initialized_) render_control_->invalidate();
    DropBuffers();
  }

  bool Resize(int width) {
    if (root_item_ == nullptr || width <= 0) return false;
    if (buffers_[0] != nullptr && buffers_[0]->Handle()->width == width) {
      return true;
    }
    std::array<SsdBuffer*, 2> next = {SsdBuffer::Create(width, kToolbarHeight),
                                      SsdBuffer::Create(width, kToolbarHeight)};
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
    window_->setGeometry(0, 0, width, kToolbarHeight);
    window_->contentItem()->setSize(QSizeF(width, kToolbarHeight));
    root_item_->setSize(QSizeF(width, kToolbarHeight));
    if (!initialized_) {
      window_->create();
      initialized_ = true;
    }
    dirty_ = true;
    return true;
  }

  void SetState(uint32_t allowed_modes, WindowSelector::Mode selected,
                std::optional<WindowSelector::Mode> hovered,
                std::optional<WindowSelector::Mode> pressed) {
    SetProperty("allowOutput",
                (allowed_modes &
                 WindowSelector::ModeMask(WindowSelector::Mode::kOutput)) != 0);
    SetProperty("allowWindow",
                (allowed_modes &
                 WindowSelector::ModeMask(WindowSelector::Mode::kWindow)) != 0);
    SetProperty("allowRegion",
                (allowed_modes &
                 WindowSelector::ModeMask(WindowSelector::Mode::kRegion)) != 0);
    SetProperty("selectedMode", static_cast<int>(selected));
    SetProperty("hoveredMode",
                hovered.has_value() ? static_cast<int>(*hovered) : -1);
    SetProperty("pressedMode",
                pressed.has_value() ? static_cast<int>(*pressed) : -1);
  }

  bool Render() {
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

  wlr_buffer* Buffer() const {
    return buffers_[current_buffer_] == nullptr
               ? nullptr
               : buffers_[current_buffer_]->Handle();
  }

 private:
  void SetProperty(const char* name, const QVariant& value) {
    if (root_item_ == nullptr || root_item_->property(name) == value) return;
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

WindowSelector::WindowSelector(wlr_scene_tree* overlay_parent, wlr_seat* seat,
                               wlr_cursor* cursor, SetCursor set_cursor,
                               HitTest hit_test)
    : seat_(seat),
      cursor_(cursor),
      set_cursor_(std::move(set_cursor)),
      hit_test_(std::move(hit_test)) {
  if (overlay_parent == nullptr) return;
  overlay_ = wlr_scene_tree_create(overlay_parent);
  if (overlay_ == nullptr) return;
  for (wlr_scene_rect*& rect : dim_) {
    rect = wlr_scene_rect_create(overlay_, 1, 1, kDimColor.data());
    if (rect == nullptr) {
      wlr_scene_node_destroy(&overlay_->node);
      overlay_ = nullptr;
      dim_.fill(nullptr);
      return;
    }
  }
  toolbar_renderer_ = std::make_unique<WindowSelectorToolbarRenderer>();
  toolbar_ = wlr_scene_buffer_create(overlay_, nullptr);
  if (toolbar_ != nullptr) toolbar_->point_accepts_input = IgnoreInput;
  wlr_scene_node_set_enabled(&overlay_->node, false);
}

WindowSelector::~WindowSelector() {
  Finish(std::nullopt, false);
  if (overlay_ != nullptr) {
    wlr_scene_node_destroy(&overlay_->node);
    overlay_ = nullptr;
    dim_.fill(nullptr);
    border_dashes_.clear();
    toolbar_ = nullptr;
  }
}

bool WindowSelector::Start(uint32_t allowed_modes, wlr_surface* mask,
                           Done done) {
  const std::vector<Mode> modes = AvailableModes(allowed_modes);
  if (active_ || overlay_ == nullptr || seat_ == nullptr ||
      cursor_ == nullptr || !set_cursor_ || !hit_test_ || !done ||
      modes.empty()) {
    return false;
  }
  allowed_modes_ = allowed_modes;
  mode_ = modes.front();
  mask_ = mask;
  if (mask_ != nullptr) mask_destroy_.Connect(&mask_->events.destroy);
  done_ = std::move(done);
  active_ = true;
  button_down_ = false;
  dragged_ = false;
  wlr_seat_pointer_clear_focus(seat_);
  set_cursor_(mode_ == Mode::kRegion ? "crosshair" : "pointer");
  wlr_scene_node_raise_to_top(&overlay_->node);
  UpdateHover();
  return true;
}

void WindowSelector::Cancel(bool notify) {
  if (active_) Finish(std::nullopt, notify);
}

bool WindowSelector::IsActive() const { return active_; }

bool WindowSelector::HandleMotion() {
  if (!active_) return false;
  if (button_down_ && mode_ == Mode::kRegion) {
    UpdateDrag();
  } else {
    UpdateHover();
  }
  return true;
}

// Both values follow the wl_pointer.button wire ABI.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
bool WindowSelector::HandleButton(uint32_t button, uint32_t state) {
  if (!active_) return false;
  if (button == BTN_RIGHT) {
    // Wait for release so the client below never observes an unmatched right
    // button release after the selector has gone away.
    if (state == WL_POINTER_BUTTON_STATE_RELEASED) Cancel();
    return true;
  }
  if (button != BTN_LEFT) return true;

  if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
    toolbar_pressed_ = ToolbarModeAt(cursor_->x, cursor_->y);
    if (toolbar_pressed_.has_value()) {
      UpdateToolbar();
      return true;
    }
    anchor_x_ = cursor_->x;
    anchor_y_ = cursor_->y;
    press_target_ = current_;
    button_down_ = true;
    dragged_ = false;
    return true;
  }

  if (toolbar_pressed_.has_value()) {
    const std::optional<Mode> released = ToolbarModeAt(cursor_->x, cursor_->y);
    const Mode pressed = *toolbar_pressed_;
    toolbar_pressed_.reset();
    if (released.has_value() && *released == pressed) {
      SetMode(pressed);
    } else {
      UpdateToolbar();
    }
    return true;
  }

  if (!button_down_) return true;
  button_down_ = false;
  const std::optional<Target> target =
      ValidTarget(current_) ? current_ : press_target_;
  if (!ValidTarget(target)) {
    dragged_ = false;
    UpdateHover();
    return true;
  }
  Finish(Selection{.mode = mode_, .target = *target}, true);
  return true;
}

bool WindowSelector::HandleKey(wlr_keyboard* keyboard,
                               const wlr_keyboard_key_event& event) {
  if (!active_) return false;
  if (keyboard == nullptr || keyboard->xkb_state == nullptr) {
    return true;
  }
  const xkb_keysym_t symbol =
      xkb_state_key_get_one_sym(keyboard->xkb_state, event.keycode + 8);
  if (event.state == WL_KEYBOARD_KEY_STATE_RELEASED) {
    // As with pointer buttons, finish on release so both halves of the key
    // sequence remain owned by the selector.
    if (symbol == XKB_KEY_Escape) {
      Cancel();
    } else if ((symbol == XKB_KEY_Return || symbol == XKB_KEY_KP_Enter) &&
               ValidTarget(current_)) {
      Finish(Selection{.mode = mode_, .target = *current_}, true);
    }
  }
  return true;
}

void WindowSelector::SurfaceUnavailable(wlr_surface* surface) {
  if (!active_ || surface == nullptr) return;
  if ((current_.has_value() && current_->surface == surface) ||
      (press_target_.has_value() && press_target_->surface == surface)) {
    current_.reset();
    press_target_.reset();
    button_down_ = false;
    dragged_ = false;
    UpdateOverlay();
  }
}

void WindowSelector::OutputUnavailable(wlr_output* output) {
  if (!active_ || output == nullptr) return;
  if ((current_.has_value() && current_->output == output) ||
      (press_target_.has_value() && press_target_->output == output) ||
      (output_bounds_.has_value() && output_bounds_->output == output)) {
    Cancel();
  }
}

void WindowSelector::OnMaskDestroy(WindowSelector* selector, void*) {
  selector->mask_destroy_.Disconnect();
  selector->mask_ = nullptr;
  selector->Cancel();
}

void WindowSelector::UpdateHover() {
  if (!active_) return;
  // The highlight lives above normal surfaces. Hide it for hit testing so it
  // cannot obscure the window below the pointer.
  wlr_scene_node_set_enabled(&overlay_->node, false);
  toolbar_hover_ = ToolbarModeAt(cursor_->x, cursor_->y);
  if (toolbar_hover_.has_value()) {
    set_cursor_("pointer");
    UpdateOverlay();
    return;
  }
  set_cursor_(mode_ == Mode::kRegion ? "crosshair" : "pointer");
  current_ = hit_test_(mode_, cursor_->x, cursor_->y, mask_);
  output_bounds_ = hit_test_(Mode::kOutput, cursor_->x, cursor_->y, mask_);
  UpdateOverlay();
}

void WindowSelector::UpdateDrag() {
  if (!active_ || !press_target_.has_value()) return;
  const double dx = std::abs(cursor_->x - anchor_x_);
  const double dy = std::abs(cursor_->y - anchor_y_);
  if (!dragged_ && dx <= kDragThreshold && dy <= kDragThreshold) return;
  dragged_ = true;

  const std::optional<Target> bounds =
      hit_test_(Mode::kOutput, anchor_x_, anchor_y_, mask_);
  if (!bounds.has_value() || bounds->output == nullptr ||
      bounds->box.width <= 0 || bounds->box.height <= 0) {
    current_.reset();
    UpdateOverlay();
    return;
  }
  const Target output_bounds = *bounds;
  output_bounds_ = output_bounds;
  toolbar_hover_.reset();
  const int left = std::clamp(
      static_cast<int>(std::floor(std::min(anchor_x_, cursor_->x))),
      output_bounds.box.x, output_bounds.box.x + output_bounds.box.width - 1);
  const int top = std::clamp(
      static_cast<int>(std::floor(std::min(anchor_y_, cursor_->y))),
      output_bounds.box.y, output_bounds.box.y + output_bounds.box.height - 1);
  const int right =
      std::clamp(static_cast<int>(std::ceil(std::max(anchor_x_, cursor_->x))),
                 left + 1, output_bounds.box.x + output_bounds.box.width);
  const int bottom =
      std::clamp(static_cast<int>(std::ceil(std::max(anchor_y_, cursor_->y))),
                 top + 1, output_bounds.box.y + output_bounds.box.height);
  current_ = Target{
      .output = output_bounds.output,
      .surface = nullptr,
      .box = {
          .x = left, .y = top, .width = right - left, .height = bottom - top}};
  UpdateOverlay();
}

void WindowSelector::UpdateOverlay() {
  if (overlay_ == nullptr || !ValidTarget(output_bounds_)) {
    if (overlay_ != nullptr) {
      wlr_scene_node_set_enabled(&overlay_->node, false);
    }
    return;
  }

  const auto set_rect = [](wlr_scene_rect* rect, int x, int y, int width,
                           int height) {
    const bool visible = rect != nullptr && width > 0 && height > 0;
    if (rect == nullptr) return;
    wlr_scene_node_set_enabled(&rect->node, visible);
    if (!visible) return;
    wlr_scene_rect_set_size(rect, width, height);
    wlr_scene_node_set_position(&rect->node, x, y);
  };
  const wlr_box output = output_bounds_->box;
  wlr_box selection = {};
  const bool has_selection =
      ValidTarget(current_) &&
      wlr_box_intersection(&selection, &current_->box, &output);
  if (has_selection) {
    set_rect(dim_[0], output.x, output.y, output.width, selection.y - output.y);
    set_rect(dim_[1], output.x, selection.y + selection.height, output.width,
             output.y + output.height - selection.y - selection.height);
    set_rect(dim_[2], output.x, selection.y, selection.x - output.x,
             selection.height);
    set_rect(dim_[3], selection.x + selection.width, selection.y,
             output.x + output.width - selection.x - selection.width,
             selection.height);
  } else {
    set_rect(dim_[0], output.x, output.y, output.width, output.height);
    for (size_t i = 1; i < dim_.size(); ++i) {
      set_rect(dim_[i], 0, 0, 0, 0);
    }
  }

  size_t dash_count = 0;
  const auto add_dash = [&](int x, int y, int width, int height) {
    if (width <= 0 || height <= 0) return;
    if (dash_count == border_dashes_.size()) {
      wlr_scene_rect* dash =
          wlr_scene_rect_create(overlay_, 1, 1, kBorderColor.data());
      if (dash == nullptr) return;
      border_dashes_.push_back(dash);
    }
    set_rect(border_dashes_[dash_count++], x, y, width, height);
  };
  if (has_selection) {
    const int border =
        std::min({kBorderWidth, selection.width, selection.height});
    for (int offset = 0; offset < selection.width;
         offset += kDashLength + kDashGap) {
      const int length = std::min(kDashLength, selection.width - offset);
      add_dash(selection.x + offset, selection.y, length, border);
      add_dash(selection.x + offset, selection.y + selection.height - border,
               length, border);
    }
    for (int offset = kDashLength + kDashGap;
         offset < selection.height - border; offset += kDashLength + kDashGap) {
      const int length =
          std::min(kDashLength, selection.height - border - offset);
      add_dash(selection.x, selection.y + offset, border, length);
      add_dash(selection.x + selection.width - border, selection.y + offset,
               border, length);
    }
  }
  for (size_t i = dash_count; i < border_dashes_.size(); ++i) {
    wlr_scene_node_set_enabled(&border_dashes_[i]->node, false);
  }

  UpdateToolbar();
  wlr_scene_node_set_enabled(&overlay_->node, true);
  wlr_scene_node_raise_to_top(&overlay_->node);
}

void WindowSelector::UpdateToolbar() {
  const std::vector<Mode> modes = AvailableModes(allowed_modes_);
  if (toolbar_ == nullptr || toolbar_renderer_ == nullptr ||
      !ValidTarget(output_bounds_) || modes.size() <= 1) {
    if (toolbar_ != nullptr) wlr_scene_node_set_enabled(&toolbar_->node, false);
    return;
  }
  const int width = static_cast<int>(modes.size()) * kToolbarButtonSize +
                    2 * kToolbarHorizontalPadding;
  if (!toolbar_renderer_->Resize(width)) {
    wlr_scene_node_set_enabled(&toolbar_->node, false);
    return;
  }
  toolbar_renderer_->SetState(allowed_modes_, mode_, toolbar_hover_,
                              toolbar_pressed_);
  if (toolbar_renderer_->Render()) {
    wlr_scene_buffer_set_buffer_with_damage(
        toolbar_, toolbar_renderer_->Buffer(), nullptr);
  }
  const wlr_box& output = output_bounds_->box;
  wlr_scene_node_set_position(
      &toolbar_->node, output.x + (output.width - width) / 2,
      output.y + output.height - kToolbarBottomMargin - kToolbarHeight);
  wlr_scene_node_set_enabled(&toolbar_->node, true);
  wlr_scene_node_raise_to_top(&toolbar_->node);
}

void WindowSelector::SetMode(Mode mode) {
  if ((allowed_modes_ & ModeMask(mode)) == 0) return;
  mode_ = mode;
  button_down_ = false;
  dragged_ = false;
  current_.reset();
  press_target_.reset();
  toolbar_hover_.reset();
  set_cursor_(mode_ == Mode::kRegion ? "crosshair" : "pointer");
  UpdateHover();
}

std::optional<WindowSelector::Mode> WindowSelector::ToolbarModeAt(
    double layout_x, double layout_y) const {
  const std::vector<Mode> modes = AvailableModes(allowed_modes_);
  if (!active_ || !ValidTarget(output_bounds_) || modes.size() <= 1) {
    return std::nullopt;
  }
  const int width = static_cast<int>(modes.size()) * kToolbarButtonSize +
                    2 * kToolbarHorizontalPadding;
  const wlr_box& output = output_bounds_->box;
  const int toolbar_x = output.x + (output.width - width) / 2;
  const int toolbar_y =
      output.y + output.height - kToolbarBottomMargin - kToolbarHeight;
  if (layout_x < toolbar_x + kToolbarHorizontalPadding ||
      layout_x >= toolbar_x + width - kToolbarHorizontalPadding ||
      layout_y < toolbar_y + (kToolbarHeight - kToolbarButtonSize) / 2 ||
      layout_y >= toolbar_y + (kToolbarHeight + kToolbarButtonSize) / 2) {
    return std::nullopt;
  }
  const size_t index = static_cast<size_t>(
      (layout_x - toolbar_x - kToolbarHorizontalPadding) / kToolbarButtonSize);
  return index < modes.size() ? std::optional<Mode>(modes[index])
                              : std::nullopt;
}

void WindowSelector::Finish(std::optional<Selection> selection, bool notify) {
  Done done = std::move(done_);
  ResetVisuals();
  if (notify && done) done(selection);
}

void WindowSelector::ResetVisuals() {
  active_ = false;
  button_down_ = false;
  dragged_ = false;
  mask_ = nullptr;
  mask_destroy_.Disconnect();
  current_.reset();
  press_target_.reset();
  output_bounds_.reset();
  toolbar_hover_.reset();
  toolbar_pressed_.reset();
  allowed_modes_ = 0;
  done_ = {};
  if (overlay_ != nullptr) wlr_scene_node_set_enabled(&overlay_->node, false);
  if (set_cursor_) set_cursor_("default");
}

}  // namespace view
}  // namespace flakewm
