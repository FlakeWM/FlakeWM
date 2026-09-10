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
 * Originally copyright by (C) 2024 KylinSoft Co., Ltd.
 * Original license: GPL-1.0-or-later, see Open Kylin Wayland Compositor.
 * Redistributed with GPL-3.0-or-later.
 * Original code is modified to adapt C++ and Wlroots 0.20.2.
 */

#ifndef SRC_PROTOCOL_FOREIGN_TOPLEVEL_FOREIGN_TOPLEVEL_FOREIGN_TOPLEVEL_H_
#define SRC_PROTOCOL_FOREIGN_TOPLEVEL_FOREIGN_TOPLEVEL_FOREIGN_TOPLEVEL_H_

#include <string>

#include "src/utils/signal_listener.h"
#include "src/wlr_wrapper/wlroots.h"

namespace flakewm {
namespace protocol {

class ProtocolManager;

class ForeignToplevel final {
 public:
  ForeignToplevel(ProtocolManager* manager,
                  wlr_foreign_toplevel_manager_v1* foreign_manager,
                  wlr_ext_foreign_toplevel_list_v1* ext_foreign_list,
                  wlr_surface* surface);
  ~ForeignToplevel();

  ForeignToplevel(const ForeignToplevel&) = delete;
  ForeignToplevel& operator=(const ForeignToplevel&) = delete;

  wlr_surface* Surface() const;
  wlr_foreign_toplevel_handle_v1* Handle() const;
  wlr_ext_foreign_toplevel_handle_v1* ExtHandle() const;
  wlr_ext_image_capture_source_v1* CaptureSource(wlr_scene_node* node,
                                                 wl_event_loop* event_loop,
                                                 wlr_allocator* allocator,
                                                 wlr_renderer* renderer);
  bool IsValid() const;
  void SetTitle(const char* title);
  void SetAppId(const char* app_id);
  void SetMaximized(bool maximized);
  void SetMinimized(bool minimized);
  void SetActivated(bool activated);
  void SetFullscreen(bool fullscreen);
  void SetOutput(wlr_output* output);
  void SetParent(wlr_foreign_toplevel_handle_v1* parent);

 private:
  static void OnRequestMaximize(
      ForeignToplevel* foreign,
      wlr_foreign_toplevel_handle_v1_maximized_event* event);
  static void OnRequestMinimize(
      ForeignToplevel* foreign,
      wlr_foreign_toplevel_handle_v1_minimized_event* event);
  static void OnRequestActivate(
      ForeignToplevel* foreign,
      wlr_foreign_toplevel_handle_v1_activated_event* event);
  static void OnRequestFullscreen(
      ForeignToplevel* foreign,
      wlr_foreign_toplevel_handle_v1_fullscreen_event* event);
  static void OnRequestClose(ForeignToplevel* foreign, void*);
  static void OnOutputDestroy(ForeignToplevel* foreign, void*);
  static void OnDestroy(ForeignToplevel* foreign, void*);
  static void OnExtDestroy(ForeignToplevel* foreign, void*);
  static void OnCaptureSourceDestroy(ForeignToplevel* foreign, void*);

  ProtocolManager* manager_;
  wlr_surface* surface_;
  wlr_foreign_toplevel_handle_v1* handle_;
  wlr_ext_foreign_toplevel_handle_v1* ext_handle_ = nullptr;
  wlr_ext_image_capture_source_v1* capture_source_ = nullptr;
  wlr_output* output_ = nullptr;
  utils::SignalListener<ForeignToplevel,
                        wlr_foreign_toplevel_handle_v1_maximized_event>
      request_maximize_{this, OnRequestMaximize};
  utils::SignalListener<ForeignToplevel,
                        wlr_foreign_toplevel_handle_v1_minimized_event>
      request_minimize_{this, OnRequestMinimize};
  utils::SignalListener<ForeignToplevel,
                        wlr_foreign_toplevel_handle_v1_activated_event>
      request_activate_{this, OnRequestActivate};
  utils::SignalListener<ForeignToplevel,
                        wlr_foreign_toplevel_handle_v1_fullscreen_event>
      request_fullscreen_{this, OnRequestFullscreen};
  utils::SignalListener<ForeignToplevel, void> request_close_{this,
                                                              OnRequestClose};
  utils::SignalListener<ForeignToplevel, void> output_destroy_{this,
                                                               OnOutputDestroy};
  utils::SignalListener<ForeignToplevel, void> destroy_{this, OnDestroy};
  utils::SignalListener<ForeignToplevel, void> ext_destroy_{this, OnExtDestroy};
  utils::SignalListener<ForeignToplevel, void> capture_source_destroy_{
      this, OnCaptureSourceDestroy};
};

}  // namespace protocol
}  // namespace flakewm

#endif  // SRC_PROTOCOL_FOREIGN_TOPLEVEL_FOREIGN_TOPLEVEL_FOREIGN_TOPLEVEL_H_
