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

#ifndef SRC_VIEW_SSD_TITLEBAR_TOOLTIP_TITLEBAR_TOOLTIP_H_
#define SRC_VIEW_SSD_TITLEBAR_TOOLTIP_TITLEBAR_TOOLTIP_H_

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

class TitlebarTooltip final {
 public:
  enum class Hint { kNone, kMinimize, kMaximize, kRestore, kClose };
  using ScreenGeometry = std::function<wlr_box(double x, double y)>;
  using SetBlur = std::function<bool(const void*, wlr_texture*,
                                     const pixman_region32_t*, float)>;
  using ClearBlur = std::function<void(const void*)>;

  TitlebarTooltip(wlr_scene_tree* overlay_parent,
                  ScreenGeometry screen_geometry, SetBlur set_blur,
                  ClearBlur clear_blur);
  ~TitlebarTooltip();

  TitlebarTooltip(const TitlebarTooltip&) = delete;
  TitlebarTooltip& operator=(const TitlebarTooltip&) = delete;

  void Hover(wlr_surface* surface, Hint hint, double cursor_x, double cursor_y);
  void SurfaceUnavailable(wlr_surface* surface);
  void Cancel();
  void Render();

 private:
  void ShowNow();
  void HideAnimated();
  void FinishHide();
  void ApplyAnimationFrame(double value, bool finished);
  bool EnsureBlurBuffer(int width, int height);
  void ClearRegisteredBlur();
  static void OnBlurNodeSample(TitlebarTooltip* tooltip,
                               wlr_scene_output_sample_event* event);

  wlr_scene_tree* tree_ = nullptr;
  wlr_scene_buffer* blur_node_ = nullptr;
  wlr_scene_buffer* node_ = nullptr;
  SsdBuffer* blur_buffer_ = nullptr;
  wlr_texture* registered_blur_texture_ = nullptr;
  std::unique_ptr<PopupRenderer> renderer_;
  std::unique_ptr<PopupAnimation> animation_;
  ScreenGeometry screen_geometry_;
  SetBlur set_blur_;
  ClearBlur clear_blur_;
  QTimer show_timer_;
  QTimer auto_hide_timer_;
  wlr_surface* surface_ = nullptr;
  Hint hint_ = Hint::kNone;
  double cursor_x_ = 0;
  double cursor_y_ = 0;
  int width_ = 0;
  int height_ = 0;
  int x_ = 0;
  int y_ = 0;
  bool visible_ = false;
  utils::SignalListener<TitlebarTooltip, wlr_scene_output_sample_event>
      blur_node_sample_{this, OnBlurNodeSample};
};

}  // namespace view
}  // namespace flakewm

#endif  // SRC_VIEW_SSD_TITLEBAR_TOOLTIP_TITLEBAR_TOOLTIP_H_
