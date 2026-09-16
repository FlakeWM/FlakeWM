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

#ifndef SRC_VIEW_SSD_SPLIT_SCREEN_SWITCHER_SPLIT_SCREEN_SWITCHER_H_
#define SRC_VIEW_SSD_SPLIT_SCREEN_SWITCHER_SPLIT_SCREEN_SWITCHER_H_

#include <QTimer>
#include <functional>
#include <memory>

#include "src/utils/signal_listener.h"
#include "src/wlr_wrapper/wlroots.h"

namespace flakewm {
namespace view {

class PopupRenderer;
class PopupAnimation;
class SsdBuffer;

class SplitScreenSwitcher final {
 public:
  enum class Tile {
    kLeft,
    kRight,
    kTopLeft,
    kBottomLeft,
    kTopRight,
    kBottomRight,
  };
  using Activate = std::function<void(wlr_surface*, Tile)>;
  using ScreenGeometry = std::function<wlr_box(double x, double y)>;
  using SetBlur = std::function<bool(const void*, wlr_texture*,
                                     const pixman_region32_t*, float)>;
  using ClearBlur = std::function<void(const void*)>;

  SplitScreenSwitcher(wlr_scene_tree* overlay_parent, Activate activate,
                      ScreenGeometry screen_geometry, SetBlur set_blur,
                      ClearBlur clear_blur);
  ~SplitScreenSwitcher();

  SplitScreenSwitcher(const SplitScreenSwitcher&) = delete;
  SplitScreenSwitcher& operator=(const SplitScreenSwitcher&) = delete;

  void HoverMaximize(wlr_surface* surface, double cursor_x, double cursor_y,
                     int client_top);
  void LeaveMaximize();
  bool HandleMotion(double x, double y);
  bool HandleButton(uint32_t button, wl_pointer_button_state state);
  void SurfaceUnavailable(wlr_surface* surface);
  void Cancel();
  void Render();
  bool IsVisible() const;

 private:
  void ShowNow();
  void UpdateView();
  void HideAnimated();
  void FinishHide();
  void ApplyAnimationFrame(double value, bool finished);
  int ItemAt(double x, double y) const;
  bool EnsureBlurBuffer();
  void ClearRegisteredBlur();
  static void OnBlurNodeSample(SplitScreenSwitcher* switcher,
                               wlr_scene_output_sample_event* event);

  wlr_scene_tree* tree_ = nullptr;
  wlr_scene_buffer* blur_node_ = nullptr;
  wlr_scene_buffer* node_ = nullptr;
  SsdBuffer* blur_buffer_ = nullptr;
  wlr_texture* registered_blur_texture_ = nullptr;
  std::unique_ptr<PopupRenderer> renderer_;
  std::unique_ptr<PopupAnimation> animation_;
  Activate activate_;
  ScreenGeometry screen_geometry_;
  SetBlur set_blur_;
  ClearBlur clear_blur_;
  QTimer show_timer_;
  wlr_surface* surface_ = nullptr;
  double cursor_x_ = 0;
  double cursor_y_ = 0;
  int client_top_ = 0;
  int x_ = 0;
  int y_ = 0;
  int hovered_item_ = -1;
  int pressed_item_ = -1;
  bool active_ = false;
  bool visible_ = false;
  bool pointer_inside_ = false;
  double animation_value_ = 0.0;
  utils::SignalListener<SplitScreenSwitcher, wlr_scene_output_sample_event>
      blur_node_sample_{this, OnBlurNodeSample};
};

}  // namespace view
}  // namespace flakewm

#endif  // SRC_VIEW_SSD_SPLIT_SCREEN_SWITCHER_SPLIT_SCREEN_SWITCHER_H_
