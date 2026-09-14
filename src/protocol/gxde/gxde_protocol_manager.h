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
 * This is an implementation of GXDE Screenshot protocol.
 */

#ifndef SRC_PROTOCOL_GXDE_GXDE_PROTOCOL_MANAGER_H_
#define SRC_PROTOCOL_GXDE_GXDE_PROTOCOL_MANAGER_H_

#include <memory>

#include "src/wlr_wrapper/wlroots.h"

namespace flakewm {
namespace core {
class CompositorPrivate;
}  // namespace core

namespace protocol {

class GxdeScreenshotManager;
class ProtocolManager;

class GxdeProtocolManager final {
 public:
  GxdeProtocolManager(core::CompositorPrivate* compositor,
                      ProtocolManager* protocol_manager);
  ~GxdeProtocolManager();

  GxdeProtocolManager(const GxdeProtocolManager&) = delete;
  GxdeProtocolManager& operator=(const GxdeProtocolManager&) = delete;

  bool Create(wl_display* display);

 private:
  friend class GxdeScreenshotManager;

  struct WindowTarget {
    wlr_surface* surface = nullptr;
    wlr_scene_node* scene_node = nullptr;
    wlr_ext_image_capture_source_v1* scene_source = nullptr;
  };

  wlr_output* FindOutput(const char* identifier) const;
  WindowTarget FindWindow(const char* identifier, const char* app_id,
                          bool without_decoration) const;
  bool CaptureAllowed() const;
  wl_event_loop* EventLoop() const;
  wlr_renderer* Renderer() const;

  core::CompositorPrivate* compositor_;
  ProtocolManager* protocol_manager_;
  wl_global* identifier_global_ = nullptr;
  std::unique_ptr<GxdeScreenshotManager> screenshot_manager_;
};

}  // namespace protocol
}  // namespace flakewm

#endif  // SRC_PROTOCOL_GXDE_GXDE_PROTOCOL_MANAGER_H_
