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
 * Server-side window decoration.
 */

#ifndef SRC_VIEW_SSD_SSD_SSD_H_
#define SRC_VIEW_SSD_SSD_SSD_H_

#include <cstdint>
#include <memory>
#include <string>

#include "src/utils/signal_listener.h"
#include "src/view/ssd/ssd_renderer/ssd_renderer.h"
#include "src/view/ssd/ssd_shadow/ssd_shadow.h"
#include "src/wlr_wrapper/wlroots.h"

namespace flakewm {
namespace view {

class Ssd final {
 public:
  enum class Part {
    kNone = 0,
    kTitlebar = 1,
    kMinimize = 2,
    kMaximize = 3,
    kClose = 4,
    kResize = 5,
  };

  struct HitTarget {
    Part part = Part::kNone;
    uint32_t edges = 0;
  };

  static std::unique_ptr<Ssd> Create(wlr_scene_tree* parent);
  ~Ssd();

  Ssd(const Ssd&) = delete;
  Ssd& operator=(const Ssd&) = delete;

  void SetGeometry(const wlr_box& client_geometry);
  void SetActive(bool active);
  void SetMaximized(bool maximized);
  void SetDialog(bool dialog);
  void SetCapabilities(bool can_minimize, bool can_maximize);
  void SetTitle(const std::string& title);
  void SetAppId(const std::string& app_id);
  void SetHovered(HitTarget target);
  void SetPressed(HitTarget target);
  void Render();

  wlr_box FrameGeometry(const wlr_box& client_geometry) const;
  wlr_box ContentGeometry(const wlr_box& frame_geometry) const;
  HitTarget HitTest(double x, double y) const;
  int TitlebarHeight() const;

 private:
  explicit Ssd(wlr_scene_tree* parent);

  bool Initialize();
  void Arrange();
  int BorderWidth() const;
  static void OnTreeDestroy(Ssd* ssd, void*);

  wlr_scene_tree* tree_ = nullptr;
  wlr_scene_buffer* titlebar_ = nullptr;
  wlr_box client_geometry_ = {};
  SsdRenderer renderer_;
  std::unique_ptr<SsdShadow> shadow_;
  bool active_ = false;
  bool maximized_ = false;
  bool dialog_ = false;
  bool can_minimize_ = true;
  bool can_maximize_ = true;
  utils::SignalListener<Ssd, void> tree_destroy_{this, OnTreeDestroy};
};

}  // namespace view
}  // namespace flakewm

#endif  // SRC_VIEW_SSD_SSD_SSD_H_
