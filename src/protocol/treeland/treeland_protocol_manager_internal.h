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
 * Originally copyright by (C) 2024-2026 UnionTech Software Technology Co., Ltd.
 * Original license: Apache-2.0 OR LGPL-3.0-only OR GPL-2.0-only OR
 *                   GPL-3.0-only.
 * Redistributed with GPL-3.0-only.
 * Original code is modified to adapt C++ and Wlroots 0.20.2.
 */

#ifndef SRC_PROTOCOL_TREELAND_TREELAND_PROTOCOL_MANAGER_INTERNAL_H_
#define SRC_PROTOCOL_TREELAND_TREELAND_PROTOCOL_MANAGER_INTERNAL_H_

#include <memory>
#include <string>
#include <vector>

#include "src/protocol/treeland/treeland_protocol_manager.h"

namespace flakewm {
namespace view {
class WindowSelector;
}  // namespace view

namespace protocol {

class TreelandGlobal {
 public:
  virtual ~TreelandGlobal() = default;
  virtual bool IsValid() const = 0;
};

class TreelandProtocolManagerImpl final {
 public:
  TreelandProtocolManagerImpl(core::CompositorPrivate* compositor,
                              ProtocolManager* protocol_manager);
  ~TreelandProtocolManagerImpl();

  bool Create(wl_display* display, wlr_seat* seat,
              wlr_output_layout* output_layout);

  wlr_output* OutputAtCursor() const;
  bool CaptureAllowed() const;
  core::CompositorPrivate* Compositor() const { return compositor; }
  ProtocolManager* Protocols() const { return protocol_manager; }
  wlr_seat* Seat() const { return seat; }
  wlr_cursor* Cursor() const;
  wlr_output_layout* OutputLayout() const { return output_layout; }
  wlr_renderer* Renderer() const;
  wlr_allocator* Allocator() const;
  wl_event_loop* EventLoop() const;
  view::WindowSelector* Selector() const;

  void ApplySurfacePosition(wlr_surface* surface, int x, int y) const;
  void ApplySurfaceOverlay(wlr_surface* surface) const;
  void ApplySurfaceAutoPlacement(wlr_surface* surface, int y_offset) const;
  void ActivateSurface(wlr_surface* surface) const;
  void SetMinimized(wlr_surface* surface, bool minimized) const;
  void SetMaximized(wlr_surface* surface, bool maximized) const;
  void SetFullscreen(wlr_surface* surface, bool fullscreen) const;
  void SplitSurface(wlr_surface* surface, bool left) const;
  void SetTitlebar(wlr_surface* surface, bool enabled) const;
  void SetBlur(wlr_surface* surface, bool enabled) const;
  bool SurfaceGeometry(wlr_surface* surface, wlr_box* geometry) const;

  core::CompositorPrivate* compositor;
  ProtocolManager* protocol_manager;
  wl_display* display = nullptr;
  wlr_seat* seat = nullptr;
  wlr_output_layout* output_layout = nullptr;
  std::vector<std::unique_ptr<TreelandGlobal>> globals;

  std::string cursor_theme;
  uint32_t cursor_size = 24;
  std::string font = "Noto Sans";
  std::string monospace_font = "Noto Sans Mono";
  uint32_t font_size = 10;
  int32_t round_corner_radius = 7;
  std::string icon_theme;
  std::string active_color;
  uint32_t window_opacity = 100;
  uint32_t window_theme_type = 1;
  uint32_t window_titlebar_height = 28;
  std::string wallpaper_metadata;
};

std::unique_ptr<TreelandGlobal> CreateTreelandAppIdResolverGlobal(
    TreelandProtocolManagerImpl* owner, wl_display* display);
std::unique_ptr<TreelandGlobal> CreateTreelandCaptureGlobal(
    TreelandProtocolManagerImpl* owner, wl_display* display);
std::unique_ptr<TreelandGlobal> CreateTreelandDdeShellGlobal(
    TreelandProtocolManagerImpl* owner, wl_display* display);
std::unique_ptr<TreelandGlobal> CreateTreelandPersonalizationGlobal(
    TreelandProtocolManagerImpl* owner, wl_display* display);
std::unique_ptr<TreelandGlobal> CreateLegacyDdeShellGlobal(
    TreelandProtocolManagerImpl* owner, wl_display* display);

}  // namespace protocol
}  // namespace flakewm

#endif  // SRC_PROTOCOL_TREELAND_TREELAND_PROTOCOL_MANAGER_INTERNAL_H_
