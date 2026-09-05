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
 * Chameleon-compatible window shadow.
 */

#ifndef SRC_VIEW_SSD_SSD_SHADOW_SSD_SHADOW_H_
#define SRC_VIEW_SSD_SSD_SHADOW_SSD_SHADOW_H_

#include "src/utils/signal_listener.h"
#include "src/view/ssd/ssd_buffer/ssd_buffer.h"
#include "src/wlr_wrapper/wlroots.h"

namespace flakewm {
namespace view {

class SsdShadow final {
 public:
  explicit SsdShadow(wlr_scene_tree* parent);
  ~SsdShadow();

  SsdShadow(const SsdShadow&) = delete;
  SsdShadow& operator=(const SsdShadow&) = delete;

  void Update(const wlr_box& frame, bool active, bool dialog, bool maximized);

 private:
  static bool RejectInput(wlr_scene_buffer*, double*, double*);
  static void OnNodeDestroy(SsdShadow* shadow, void*);
  void DropBuffer();

  wlr_scene_buffer* node_ = nullptr;
  SsdBuffer* buffer_ = nullptr;
  wlr_box frame_ = {};
  bool active_ = false;
  bool dialog_ = false;
  bool maximized_ = false;
  utils::SignalListener<SsdShadow, void> node_destroy_{this, OnNodeDestroy};
};

}  // namespace view
}  // namespace flakewm

#endif  // SRC_VIEW_SSD_SSD_SHADOW_SSD_SHADOW_H_
