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
 * Compositor-drawn shadow + border for DTK xdg_popup surfaces (menus).
 */

#ifndef SRC_VIEW_SSD_POPUP_SHADOW_POPUP_SHADOW_H_
#define SRC_VIEW_SSD_POPUP_SHADOW_POPUP_SHADOW_H_

#include "src/utils/signal_listener.h"
#include "src/view/ssd/ssd_buffer/ssd_buffer.h"
#include "src/wlr_wrapper/wlroots.h"

namespace flakewm {
namespace view {

// A subtle black shadow plus a one-pixel border drawn around a popup's content
// box, matching gxde-wlcom's popup decoration. DTK menus (client-side) do not
// draw their own shadow or border under Wayland; the compositor owns both. The
// shadow node lives at the bottom of the popup's scene tree so the popup
// surface is composited on top of it; the border is a separate node raised
// above the surface so its stroke is visible on the content edge (gxde-wlcom
// draws the border over the content with the texture shader, not under it).
class PopupShadow final {
 public:
  explicit PopupShadow(wlr_scene_tree* parent);
  ~PopupShadow();

  PopupShadow(const PopupShadow&) = delete;
  PopupShadow& operator=(const PopupShadow&) = delete;

  // `frame` is the popup content box in the popup scene tree's coordinates,
  // which coincides with the xdg geometry origin (see wlroots scene xdg_shell).
  void Update(const wlr_box& frame);

  // Selects the border color for the current window theme: black ~20% on the
  // light theme, white ~12% on the dark theme (gxde-wlcom's border colors,
  // with the light theme pushed stronger so the 1px edge stays visible).
  void SetDark(bool dark);

 private:
  static bool RejectInput(wlr_scene_buffer*, double*, double*);
  static void OnNodeDestroy(PopupShadow* shadow, void*);
  static void OnBorderNodeDestroy(PopupShadow* shadow, void*);
  void DropBuffer();
  void DropBorderBuffer();

  wlr_scene_buffer* node_ = nullptr;
  SsdBuffer* buffer_ = nullptr;
  wlr_scene_buffer* border_node_ = nullptr;
  SsdBuffer* border_buffer_ = nullptr;
  wlr_box frame_ = {};
  bool dark_ = false;
  bool rendered_dark_ = false;
  utils::SignalListener<PopupShadow, void> node_destroy_{this, OnNodeDestroy};
  utils::SignalListener<PopupShadow, void> border_node_destroy_{
      this, OnBorderNodeDestroy};
};

}  // namespace view
}  // namespace flakewm

#endif  // SRC_VIEW_SSD_POPUP_SHADOW_POPUP_SHADOW_H_
