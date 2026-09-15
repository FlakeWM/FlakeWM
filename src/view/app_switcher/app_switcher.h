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
 * The layout is adapted from GXDE KWin's thumbnail_grid window switcher.
 */

#ifndef SRC_VIEW_APP_SWITCHER_APP_SWITCHER_H_
#define SRC_VIEW_APP_SWITCHER_APP_SWITCHER_H_

#include <QString>
#include <functional>
#include <memory>
#include <vector>

#include "src/utils/signal_listener.h"
#include "src/wlr_wrapper/wlroots.h"

namespace flakewm {
namespace view {

class AppSwitcherRenderer;
class SsdBuffer;

class AppSwitcher final {
 public:
  struct Entry {
    wlr_surface* surface = nullptr;
    QString title;
    QString app_id;
    bool active = false;
    bool minimized = false;
  };

  using EntriesProvider = std::function<std::vector<Entry>()>;
  using Activate = std::function<void(wlr_surface*)>;
  using ScreenGeometry = std::function<wlr_box()>;
  using SetBlur = std::function<bool(const void*, wlr_texture*,
                                     const pixman_region32_t*, float)>;
  using ClearBlur = std::function<void(const void*)>;

  AppSwitcher(wlr_scene_tree* overlay_parent, EntriesProvider entries_provider,
              Activate activate, ScreenGeometry screen_geometry,
              SetBlur set_blur, ClearBlur clear_blur);
  ~AppSwitcher();

  AppSwitcher(const AppSwitcher&) = delete;
  AppSwitcher& operator=(const AppSwitcher&) = delete;

  bool Cycle(bool reverse);
  bool HandleKey(wlr_keyboard* keyboard, const wlr_keyboard_key_event& event);
  void SurfaceActivated(wlr_surface* surface);
  void SurfaceUnavailable(wlr_surface* surface);
  void Cancel();
  bool IsActive() const;

 private:
  void RefreshEntries(wlr_surface* preferred);
  void Step(bool reverse);
  void UpdateView();
  void Finish(bool activate);
  bool EnsureBlurBuffer(int width, int height);
  void ClearRegisteredBlur();
  static void OnBlurNodeSample(AppSwitcher* switcher,
                               wlr_scene_output_sample_event* event);

  wlr_scene_tree* tree_ = nullptr;
  wlr_scene_buffer* blur_node_ = nullptr;
  wlr_scene_buffer* node_ = nullptr;
  SsdBuffer* blur_buffer_ = nullptr;
  wlr_texture* registered_blur_texture_ = nullptr;
  std::unique_ptr<AppSwitcherRenderer> renderer_;
  EntriesProvider entries_provider_;
  Activate activate_;
  ScreenGeometry screen_geometry_;
  SetBlur set_blur_;
  ClearBlur clear_blur_;
  std::vector<Entry> entries_;
  std::vector<wlr_surface*> focus_history_;
  int current_index_ = -1;
  bool active_ = false;
  utils::SignalListener<AppSwitcher, wlr_scene_output_sample_event>
      blur_node_sample_{this, OnBlurNodeSample};
};

}  // namespace view
}  // namespace flakewm

#endif  // SRC_VIEW_APP_SWITCHER_APP_SWITCHER_H_
