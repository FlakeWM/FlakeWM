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

#ifndef SRC_VIEW_SSD_SPLIT_SCREEN_SWITCHER_TILE_ANIMATION_H_
#define SRC_VIEW_SSD_SPLIT_SCREEN_SWITCHER_TILE_ANIMATION_H_

#include <QElapsedTimer>
#include <QTimer>
#include <vector>

#include "src/wlr_wrapper/wlroots.h"

namespace flakewm {
namespace view {

class TileAnimation final {
 public:
  explicit TileAnimation(wlr_scene_tree* overlay_parent);
  ~TileAnimation();

  TileAnimation(const TileAnimation&) = delete;
  TileAnimation& operator=(const TileAnimation&) = delete;

  bool Start(wlr_scene_tree* source, const wlr_box& from,
             const wlr_box& to);
  void SourceUnavailable(wlr_scene_tree* source);
  void Cancel();

 private:
  struct Snapshot {
    wlr_scene_buffer* node = nullptr;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
  };

  struct CaptureContext {
    TileAnimation* animation;
    int origin_x;
    int origin_y;
  };

  void Update();
  void Complete(bool restore_source);
  static void CaptureBuffer(wlr_scene_buffer* buffer, int x, int y,
                            void* data);

  wlr_scene_tree* overlay_parent_ = nullptr;
  wlr_scene_tree* snapshot_tree_ = nullptr;
  wlr_scene_tree* source_ = nullptr;
  std::vector<Snapshot> snapshots_;
  wlr_box from_ = {};
  wlr_box to_ = {};
  QElapsedTimer clock_;
  QTimer timer_;
};

}  // namespace view
}  // namespace flakewm

#endif  // SRC_VIEW_SSD_SPLIT_SCREEN_SWITCHER_TILE_ANIMATION_H_
