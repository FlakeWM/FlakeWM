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
 * This is an implementation of GXDE Protocol Manager.
 */

#ifndef SRC_PROTOCOL_GXDE_GXDE_PROTOCOL_MANAGER_INTERNAL_H_
#define SRC_PROTOCOL_GXDE_GXDE_PROTOCOL_MANAGER_INTERNAL_H_

#include <cstdint>
#include <memory>
#include <vector>

#include "src/protocol/gxde/gxde_protocol_manager.h"

namespace flakewm {
namespace protocol {

wl_global* CreateGxdeIdentifierGlobal(wl_display* display);

class GxdeScreenshotManager {
 public:
  GxdeScreenshotManager(GxdeProtocolManager* owner, wl_display* display);
  ~GxdeScreenshotManager();

  GxdeScreenshotManager(const GxdeScreenshotManager&) = delete;
  GxdeScreenshotManager& operator=(const GxdeScreenshotManager&) = delete;

  bool IsValid() const;

 private:
  class Frame;
  struct Binding;

  static void Bind(wl_client* client, void* data, uint32_t version,
                   uint32_t id);
  static void DestroyBinding(wl_resource* resource);
  static void DestroyManager(wl_client* client, wl_resource* resource);
  static void CaptureOutput(wl_client* client, wl_resource* resource,
                            uint32_t id, const char* output,
                            uint32_t overlay_cursor);
  static void CaptureOutputRegion(wl_client* client, wl_resource* resource,
                                  uint32_t id, const char* output, int32_t x,
                                  int32_t y, uint32_t width, uint32_t height,
                                  uint32_t overlay_cursor);
  static void CaptureWindow(wl_client* client, wl_resource* resource,
                            uint32_t id, const char* window,
                            uint32_t without_decoration);
  static void CaptureWindowThumbnail(wl_client* client, wl_resource* resource,
                                     uint32_t id, const char* app_id,
                                     const char* window, uint32_t max_width,
                                     uint32_t max_height,
                                     uint32_t without_decoration);

  Frame* CreateFrame(wl_client* client, wl_resource* manager_resource,
                     uint32_t id);
  void RemoveFrame(Frame* frame);
  void RemoveBinding(Binding* binding);

  GxdeProtocolManager* owner_;
  wl_global* global_ = nullptr;
  wlr_allocator* shm_allocator_ = nullptr;
  std::vector<Binding*> bindings_;
  std::vector<Frame*> frames_;
};

}  // namespace protocol
}  // namespace flakewm

#endif  // SRC_PROTOCOL_GXDE_GXDE_PROTOCOL_MANAGER_INTERNAL_H_
