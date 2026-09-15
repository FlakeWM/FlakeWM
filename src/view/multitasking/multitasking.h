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

#ifndef SRC_VIEW_MULTITASKING_MULTITASKING_H_
#define SRC_VIEW_MULTITASKING_MULTITASKING_H_

#include <QElapsedTimer>
#include <QString>
#include <QTimer>
#include <functional>
#include <memory>
#include <vector>

#include "src/wlr_wrapper/wlroots.h"

namespace flakewm {
namespace view {

class MultitaskingRenderer;
class SsdBuffer;

struct MultitaskingLayout {
  int width = 0;
  int height = 0;
  int workspace_x = 0;
  int workspace_y = 13;
  int workspace_width = 160;
  int workspace_height = 90;
  int workspace_gap = 27;
  int window_area_y = 116;
  int add_x = 0;
  int add_y = 0;
  int add_size = 64;
  int dragged_workspace = -1;
  int dragged_workspace_x = 0;
};

struct MultitaskingWindowPlacement {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

class Multitasking final {
 public:
  struct Entry {
    wlr_surface* surface = nullptr;
    QString title;
    QString app_id;
    wlr_box geometry = {};
    int workspace = 0;
    bool active = false;
    bool minimized = false;
    bool kept_above = false;
  };

  using EntriesProvider = std::function<std::vector<Entry>()>;
  using WorkspaceCount = std::function<int()>;
  using CurrentWorkspace = std::function<int()>;
  using SwitchWorkspace = std::function<void(int)>;
  using Activate = std::function<void(wlr_surface*)>;
  using Close = std::function<void(wlr_surface*)>;
  using ToggleKeptAbove = std::function<void(wlr_surface*)>;
  using AddWorkspace = std::function<bool()>;
  using RemoveWorkspace = std::function<bool(int)>;
  using ReorderWorkspace = std::function<bool(int, int)>;
  using MoveToWorkspace = std::function<bool(wlr_surface*, int)>;
  using SetSourcesHidden = std::function<void(bool)>;
  using ScreenGeometry = std::function<wlr_box()>;

  Multitasking(wlr_scene_tree* overlay_parent, EntriesProvider entries_provider,
               WorkspaceCount workspace_count,
               CurrentWorkspace current_workspace,
               SwitchWorkspace switch_workspace, Activate activate, Close close,
               ToggleKeptAbove toggle_kept_above, AddWorkspace add_workspace,
               RemoveWorkspace remove_workspace,
               ReorderWorkspace reorder_workspace,
               MoveToWorkspace move_to_workspace,
               SetSourcesHidden set_sources_hidden,
               ScreenGeometry screen_geometry);
  ~Multitasking();

  Multitasking(const Multitasking&) = delete;
  Multitasking& operator=(const Multitasking&) = delete;

  bool Toggle();
  bool HandleKey(wlr_keyboard* keyboard, const wlr_keyboard_key_event& event);
  bool HandleMotion(double layout_x, double layout_y);
  bool HandleButton(uint32_t button, wl_pointer_button_state state);
  void SurfaceUpdated(wlr_surface* surface);
  void SurfaceUnavailable(wlr_surface* surface);
  void Cancel();
  bool IsActive() const;

 private:
  void Refresh();
  void UpdateView();
  void CalculateWindowLayout();
  void StartAnimation(bool closing);
  void UpdateAnimation();
  void CompleteClose();
  void RebuildPreviews();
  void ClearPreviews();
  void Finish(bool activate_window);
  void SelectWorkspace(int workspace);
  void StepWindow(int delta);
  int WorkspaceX(int index) const;
  bool PointerOnCloseButton(int index) const;
  bool PointerOnTopButton(int index) const;
  bool PointerOnWorkspaceCloseButton(int index) const;
  bool PointerOnAddButton() const;
  int WorkspaceAt(double local_x, double local_y) const;
  int WindowAt(double local_x, double local_y) const;
  enum class Control {
    kNone,
    kWindow,
    kWindowClose,
    kWindowTop,
    kWorkspace,
    kWorkspaceClose,
    kAddWorkspace,
  };

  wlr_scene_tree* tree_ = nullptr;
  wlr_scene_buffer* background_node_ = nullptr;
  wlr_scene_rect* backdrop_node_ = nullptr;
  wlr_scene_tree* preview_tree_ = nullptr;
  wlr_scene_buffer* node_ = nullptr;
  SsdBuffer* background_buffer_ = nullptr;
  SsdBuffer* workspace_wallpaper_buffer_ = nullptr;
  std::unique_ptr<MultitaskingRenderer> renderer_;
  EntriesProvider entries_provider_;
  WorkspaceCount workspace_count_;
  CurrentWorkspace current_workspace_;
  SwitchWorkspace switch_workspace_;
  Activate activate_;
  Close close_;
  ToggleKeptAbove toggle_kept_above_;
  AddWorkspace add_workspace_;
  RemoveWorkspace remove_workspace_;
  ReorderWorkspace reorder_workspace_;
  MoveToWorkspace move_to_workspace_;
  SetSourcesHidden set_sources_hidden_;
  ScreenGeometry screen_geometry_;
  std::vector<Entry> entries_;
  std::vector<Entry> visible_entries_;
  std::vector<int> workspace_window_counts_;
  std::vector<MultitaskingWindowPlacement> window_placements_;
  std::vector<MultitaskingWindowPlacement> animation_from_;
  std::vector<MultitaskingWindowPlacement> animation_to_;
  MultitaskingLayout layout_;
  wlr_box screen_ = {};
  int selected_workspace_ = 0;
  int selected_window_ = -1;
  int hovered_workspace_ = -1;
  int hovered_window_ = -1;
  int pressed_workspace_ = -1;
  int pressed_window_ = -1;
  int dragged_workspace_ = -1;
  double pointer_x_ = 0.0;
  double pointer_y_ = 0.0;
  double press_x_ = 0.0;
  double press_y_ = 0.0;
  double workspace_opacity_ = 1.0;
  Control hovered_control_ = Control::kNone;
  Control pressed_control_ = Control::kNone;
  bool active_ = false;
  bool closing_ = false;
  bool animating_ = false;
  bool dragging_ = false;
  wlr_surface* pending_activation_ = nullptr;
  wlr_surface* pending_close_ = nullptr;
  int pending_workspace_ = -1;
  QTimer animation_timer_;
  QElapsedTimer animation_clock_;
};

}  // namespace view
}  // namespace flakewm

#endif  // SRC_VIEW_MULTITASKING_MULTITASKING_H_
