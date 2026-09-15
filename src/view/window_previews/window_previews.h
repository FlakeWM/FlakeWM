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

#ifndef SRC_VIEW_WINDOW_PREVIEWS_WINDOW_PREVIEWS_H_
#define SRC_VIEW_WINDOW_PREVIEWS_WINDOW_PREVIEWS_H_

#include <QElapsedTimer>
#include <QString>
#include <QTimer>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "src/wlr_wrapper/wlroots.h"

namespace flakewm {
namespace view {

class WindowPreviewsRenderer;

struct WindowPreviewBox {
  double x = 0.0;
  double y = 0.0;
  double width = 0.0;
  double height = 0.0;
};

struct WindowPreviewVisual {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  int base_x = 0;
  int base_y = 0;
  int base_width = 0;
  int base_height = 0;
  int close_x = 0;
  int close_y = 0;
  double opacity = 1.0;
  double highlight = 0.0;
  bool show_close = false;
};

class WindowPreviews final {
 public:
  struct Entry {
    wlr_surface* surface = nullptr;
    QString title;
    QString app_id;
    wlr_box geometry = {};
    int workspace = 0;
    bool active = false;
    bool minimized = false;
  };

  using EntriesProvider = std::function<std::vector<Entry>()>;
  using CurrentWorkspace = std::function<int()>;
  using Activate = std::function<void(wlr_surface*)>;
  using Close = std::function<void(wlr_surface*)>;
  using SetSourcesHidden = std::function<void(bool)>;
  using ScreenGeometry = std::function<wlr_box()>;
  using UsableGeometry = std::function<wlr_box()>;
  using CursorPosition = std::function<std::pair<double, double>()>;

  WindowPreviews(wlr_scene_tree* overlay_parent,
                 EntriesProvider entries_provider,
                 CurrentWorkspace current_workspace, Activate activate,
                 Close close, SetSourcesHidden set_sources_hidden,
                 ScreenGeometry screen_geometry, UsableGeometry usable_geometry,
                 CursorPosition cursor_position);
  ~WindowPreviews();

  WindowPreviews(const WindowPreviews&) = delete;
  WindowPreviews& operator=(const WindowPreviews&) = delete;

  bool Toggle();
  bool HandleKey(wlr_keyboard* keyboard, const wlr_keyboard_key_event& event);
  bool HandleMotion(double layout_x, double layout_y);
  bool HandleButton(uint32_t button, wl_pointer_button_state state);
  void SurfaceUpdated(wlr_surface* surface);
  void SurfaceUnavailable(wlr_surface* surface);
  void Cancel();
  bool IsActive() const;

 private:
  struct Item {
    Entry entry;
    WindowPreviewBox original;
    WindowPreviewBox target;
    WindowPreviewBox from;
    WindowPreviewBox to;
    WindowPreviewBox current;
    WindowPreviewVisual visual;
    double opacity = 1.0;
    double highlight = 1.0;
  };

  void Refresh(wlr_surface* exclude = nullptr);
  void CalculateNaturalLayout(std::vector<WindowPreviewBox>* boxes) const;
  void Reflow(wlr_surface* exclude = nullptr);
  void UpdateView();
  void UpdateVisuals();
  void RebuildPreviews();
  void ClearPreviews();
  void StartClose();
  void CompleteClose();
  void UpdateAnimation();
  void ActivateItem(int index);
  void CloseItem(int index);
  void UpdateHover();
  int ItemAt(double x, double y) const;
  bool CloseButtonAt(int index, double x, double y) const;
  int RelativeItem(int index, int xdiff, int ydiff, bool wrap) const;

  wlr_scene_tree* tree_ = nullptr;
  wlr_scene_rect* backdrop_ = nullptr;
  wlr_scene_rect* top_dim_ = nullptr;
  wlr_scene_rect* bottom_dim_ = nullptr;
  wlr_scene_rect* left_dim_ = nullptr;
  wlr_scene_rect* right_dim_ = nullptr;
  wlr_scene_tree* preview_tree_ = nullptr;
  wlr_scene_buffer* chrome_ = nullptr;
  std::unique_ptr<WindowPreviewsRenderer> renderer_;
  EntriesProvider entries_provider_;
  CurrentWorkspace current_workspace_;
  Activate activate_;
  Close close_;
  SetSourcesHidden set_sources_hidden_;
  ScreenGeometry screen_geometry_;
  UsableGeometry usable_geometry_;
  CursorPosition cursor_position_;
  std::vector<Item> items_;
  wlr_box screen_ = {};
  wlr_box usable_ = {};
  QString filter_;
  int highlighted_ = -1;
  uint32_t last_key_ = 0;
  bool key_released_ = true;
  double pointer_x_ = 0.0;
  double pointer_y_ = 0.0;
  double decal_opacity_ = 0.0;
  double backdrop_alpha_ = 0.0;
  double panel_alpha_ = 0.0;
  bool active_ = false;
  bool closing_ = false;
  QTimer animation_timer_;
  QElapsedTimer animation_clock_;
  qint64 previous_tick_ms_ = 0;
};

}  // namespace view
}  // namespace flakewm

#endif  // SRC_VIEW_WINDOW_PREVIEWS_WINDOW_PREVIEWS_H_
