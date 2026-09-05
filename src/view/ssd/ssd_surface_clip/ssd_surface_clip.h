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
 * Bottom-corner clipping for a server-decorated surface.
 */

#ifndef SRC_VIEW_SSD_SSD_SURFACE_CLIP_SSD_SURFACE_CLIP_H_
#define SRC_VIEW_SSD_SSD_SURFACE_CLIP_SSD_SURFACE_CLIP_H_

#include <memory>
#include <vector>

#include "src/utils/signal_listener.h"
#include "src/wlr_wrapper/wlroots.h"

namespace flakewm {
namespace view {

class SsdSurfaceClip final {
 public:
  static std::unique_ptr<SsdSurfaceClip> Create(wlr_scene_tree* xdg_tree,
                                                wlr_surface* surface);
  ~SsdSurfaceClip();

  SsdSurfaceClip(const SsdSurfaceClip&) = delete;
  SsdSurfaceClip& operator=(const SsdSurfaceClip&) = delete;

  void Update(const wlr_box& geometry, bool maximized);

 private:
  static constexpr int kRadius = 8;

  struct Segment {
    enum class Alignment { kLeft, kCenter, kRight };

    wlr_scene_tree* tree = nullptr;
    wlr_scene_buffer* buffer = nullptr;
    Alignment alignment = Alignment::kCenter;
    int row = 0;
    int inset = 0;
    float coverage = 1.0F;
  };

  SsdSurfaceClip(wlr_scene_tree* xdg_tree, wlr_scene_tree* surface_tree,
                 wlr_surface* surface);

  bool Initialize();
  void ClearClip();
  void ApplyCoverage();
  static void OnCommit(SsdSurfaceClip* clip, void*);
  static void OnTreeDestroy(SsdSurfaceClip* clip, void*);

  wlr_scene_tree* xdg_tree_ = nullptr;
  wlr_scene_tree* surface_tree_ = nullptr;
  wlr_surface* surface_ = nullptr;
  std::vector<Segment> segments_;
  utils::SignalListener<SsdSurfaceClip, void> commit_{this, OnCommit};
  utils::SignalListener<SsdSurfaceClip, void> tree_destroy_{this,
                                                            OnTreeDestroy};
};

}  // namespace view
}  // namespace flakewm

#endif  // SRC_VIEW_SSD_SSD_SURFACE_CLIP_SSD_SURFACE_CLIP_H_
