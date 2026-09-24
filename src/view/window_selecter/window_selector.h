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

#ifndef SRC_VIEW_WINDOW_SELECTER_WINDOW_SELECTOR_H_
#define SRC_VIEW_WINDOW_SELECTER_WINDOW_SELECTOR_H_

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "src/utils/signal_listener.h"
#include "src/wlr_wrapper/wlroots.h"

namespace flakewm {
namespace view {

class WindowSelectorToolbarRenderer;

class WindowSelector final {
 public:
  enum class Mode : uint8_t { kOutput, kWindow, kRegion };

  static constexpr uint32_t ModeMask(Mode mode) {
    return 1U << static_cast<uint8_t>(mode);
  }

  struct Target {
    wlr_output* output = nullptr;
    wlr_surface* surface = nullptr;
    wlr_box box = {};
  };

  struct Selection {
    Mode mode = Mode::kOutput;
    Target target;
  };

  using HitTest = std::function<std::optional<Target>(
      Mode mode, double layout_x, double layout_y, wlr_surface* mask)>;
  using Done = std::function<void(std::optional<Selection>)>;

  // Sets a named cursor image through the compositor, bye bye XCursor!!
  using SetCursor = std::function<void(const char* name)>;

  WindowSelector(wlr_scene_tree* overlay_parent, wlr_seat* seat,
                 wlr_cursor* cursor, SetCursor set_cursor, HitTest hit_test);
  ~WindowSelector();

  WindowSelector(const WindowSelector&) = delete;
  WindowSelector& operator=(const WindowSelector&) = delete;

  bool Start(uint32_t allowed_modes, wlr_surface* mask, Done done);
  void Cancel(bool notify = true);
  bool IsActive() const;

  bool HandleMotion();
  bool HandleButton(uint32_t button, uint32_t state);
  bool HandleKey(wlr_keyboard* keyboard, const wlr_keyboard_key_event& event);
  void SurfaceUnavailable(wlr_surface* surface);
  void OutputUnavailable(wlr_output* output);

 private:
  void UpdateHover();
  void UpdateDrag();
  void UpdateOverlay();
  void UpdateToolbar();
  void SetMode(Mode mode);
  std::optional<Mode> ToolbarModeAt(double layout_x, double layout_y) const;
  void Finish(std::optional<Selection> selection, bool notify);
  void ResetVisuals();
  static void OnMaskDestroy(WindowSelector* selector, void*);

  wlr_scene_tree* overlay_ = nullptr;
  std::array<wlr_scene_rect*, 4> dim_ = {};
  std::vector<wlr_scene_rect*> border_dashes_;
  wlr_scene_buffer* toolbar_ = nullptr;
  std::unique_ptr<WindowSelectorToolbarRenderer> toolbar_renderer_;
  wlr_seat* seat_ = nullptr;
  wlr_cursor* cursor_ = nullptr;
  SetCursor set_cursor_;
  HitTest hit_test_;
  Done done_;
  std::optional<Target> current_;
  std::optional<Target> press_target_;
  std::optional<Target> output_bounds_;
  std::optional<Mode> toolbar_hover_;
  std::optional<Mode> toolbar_pressed_;
  wlr_surface* mask_ = nullptr;
  uint32_t allowed_modes_ = 0;
  Mode mode_ = Mode::kOutput;
  double anchor_x_ = 0;
  double anchor_y_ = 0;
  bool active_ = false;
  bool button_down_ = false;
  bool dragged_ = false;
  utils::SignalListener<WindowSelector, void> mask_destroy_{this,
                                                            OnMaskDestroy};
};

}  // namespace view
}  // namespace flakewm

#endif  // SRC_VIEW_WINDOW_SELECTER_WINDOW_SELECTOR_H_
