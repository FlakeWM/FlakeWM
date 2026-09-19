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

#ifndef SRC_VIEW_SSD_WINDOW_MENU_WINDOW_MENU_H_
#define SRC_VIEW_SSD_WINDOW_MENU_WINDOW_MENU_H_

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "src/utils/signal_listener.h"
#include "src/wlr_wrapper/wlroots.h"

namespace flakewm {
namespace view {

class SsdBuffer;
class WindowMenuRenderer;

class WindowMenu final {
 public:
  enum class Action {
    kMinimize,
    kToggleMaximize,
    kMove,
    kResize,
    kToggleKeepAbove,
    kToggleAllWorkspaces,
    kMoveWorkspaceLeft,
    kMoveWorkspaceRight,
    kClose,
  };

  struct State {
    bool maximized = false;
    bool minimizable = false;
    bool maximizable = false;
    bool movable = false;
    bool resizable = false;
    bool kept_above = false;
    bool all_workspaces = false;
    int workspace = 0;
    int workspace_count = 1;
  };

  using StateProvider =
      std::function<std::optional<State>(wlr_surface* surface)>;
  using ActionHandler =
      std::function<void(wlr_surface* surface, Action action)>;
  using ScreenGeometry = std::function<wlr_box(double x, double y)>;
  using SetBlur = std::function<bool(const void*, wlr_texture*,
                                     const pixman_region32_t*, float)>;
  using ClearBlur = std::function<void(const void*)>;

  WindowMenu(wlr_scene_tree* overlay_parent, StateProvider state_provider,
             ActionHandler action_handler, ScreenGeometry screen_geometry,
             SetBlur set_blur, ClearBlur clear_blur);
  ~WindowMenu();

  WindowMenu(const WindowMenu&) = delete;
  WindowMenu& operator=(const WindowMenu&) = delete;

  bool Show(wlr_surface* surface, double x, double y);
  bool HandleMotion(double x, double y);
  bool HandleButton(uint32_t button, wl_pointer_button_state state);
  bool HandleKey(wlr_keyboard* keyboard, const wlr_keyboard_key_event& event);
  void SurfaceUnavailable(wlr_surface* surface);
  void Cancel();
  bool IsActive() const;

 private:
  struct Item {
    Action action;
    bool enabled;
    bool checked;
    bool checkable;
  };

  void RebuildItems(const State& state);
  void UpdateView();
  int ItemAt(double x, double y) const;
  void MoveHighlight(int delta);
  void Activate(int index);
  bool EnsureBlurBuffer();
  void ClearRegisteredBlur();
  static void OnBlurNodeSample(WindowMenu* menu,
                               wlr_scene_output_sample_event* event);

  wlr_scene_tree* tree_ = nullptr;
  wlr_scene_buffer* blur_node_ = nullptr;
  wlr_scene_buffer* node_ = nullptr;
  SsdBuffer* blur_buffer_ = nullptr;
  wlr_texture* registered_blur_texture_ = nullptr;
  std::unique_ptr<WindowMenuRenderer> renderer_;
  StateProvider state_provider_;
  ActionHandler action_handler_;
  ScreenGeometry screen_geometry_;
  SetBlur set_blur_;
  ClearBlur clear_blur_;
  std::vector<Item> items_;
  wlr_surface* surface_ = nullptr;
  int x_ = 0;
  int y_ = 0;
  int pointer_target_ = -1;
  int hovered_index_ = -1;
  int pressed_index_ = -1;
  bool active_ = false;
  utils::SignalListener<WindowMenu, wlr_scene_output_sample_event>
      blur_node_sample_{this, OnBlurNodeSample};
};

}  // namespace view
}  // namespace flakewm

#endif  // SRC_VIEW_SSD_WINDOW_MENU_WINDOW_MENU_H_
