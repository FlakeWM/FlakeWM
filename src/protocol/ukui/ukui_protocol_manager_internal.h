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

#ifndef SRC_PROTOCOL_UKUI_UKUI_PROTOCOL_MANAGER_INTERNAL_H_
#define SRC_PROTOCOL_UKUI_UKUI_PROTOCOL_MANAGER_INTERNAL_H_

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "src/protocol/ukui/ukui_protocol_manager.h"

namespace flakewm {
namespace protocol {

namespace ukui_internal {

inline constexpr uint32_t kBlurVersion = 1;
inline constexpr uint32_t kOutputVersion = 1;
inline constexpr uint32_t kShellVersion = 4;
inline constexpr uint32_t kWindowVersion = 1;
inline constexpr char kDesktopId[] = "flakewm-desktop-1";
inline constexpr int32_t kMaximumCoordinate =
    std::numeric_limits<int32_t>::max() / 4;

inline const char* Safe(const char* value) {
  return value == nullptr ? "" : value;
}

inline bool SameBox(const wlr_box& left, const wlr_box& right) {
  return left.x == right.x && left.y == right.y && left.width == right.width &&
         left.height == right.height;
}

}  // namespace ukui_internal

class UkuiProtocolManager::Impl final {
 public:
  struct Output;
  struct OutputBinding;
  struct ShellSurface;
  struct Window;
  struct Blur;

  explicit Impl(core::CompositorPrivate* compositor);
  ~Impl();

  bool Create(wl_display* display, wlr_seat* seat,
              wlr_output_layout* output_layout);

  struct OutputBinding {
    Output* output;
    wl_resource* resource;
  };

  struct Output {
    Impl* manager;
    wlr_output* handle;
    bool announced = false;
    wlr_box last_usable = {};
    std::vector<OutputBinding*> bindings;
    wl_listener destroy = {};
  };

  struct ShellSurface {
    Impl* manager;
    wl_resource* resource;
    wlr_surface* surface;
    wlr_output* output = nullptr;
    int32_t x = 0;
    int32_t y = 0;
    uint32_t role = 0;
    bool position_set = false;
    bool skip_taskbar = false;
    bool skip_switcher = false;
    bool panel_auto_hide = false;
    bool panel_takes_focus = false;
    bool removed_ssd = false;
    std::string icon_name;
    wl_listener surface_destroy = {};
  };

  struct Window {
    Impl* manager;
    wlr_surface* surface;
    std::string uuid;
    Window* parent = nullptr;
    uint32_t extra_state = 0;
    std::vector<wl_resource*> resources;
  };

  struct Blur {
    Impl* manager;
    wl_resource* resource;
    wlr_surface* surface;
    pixman_region32_t current_region;
    pixman_region32_t pending_region;
    uint32_t current_level = 5;
    uint32_t pending_level = 5;
    bool current_region_set = false;
    bool pending_region_set = false;
    bool current_infinite = false;
    bool pending_infinite = false;
    uint32_t pending = 0;
    wl_listener commit = {};
    wl_listener destroy = {};
  };

  void AddOutput(wlr_output* handle);
  void UpdateOutputs();
  static void BindOutputManagement(wl_client* client, void* data,
                                   uint32_t version, uint32_t id);
  static void RemoveOutputManagement(wl_resource* resource);
  void CreateOutputBinding(Output* output, wl_resource* management);
  static void DestroyOutputBinding(wl_resource* resource);
  void FinishOutput(Output* output);
  static void OnOutputDestroy(wl_listener* listener, void* data);
  void RemoveOutput(Output* output);
  Output* FindOutput(wlr_output* handle) const;

  static void DestroyResourceRequest(wl_client* client, wl_resource* resource);

  static void BindShell(wl_client* client, void* data, uint32_t version,
                        uint32_t id);
  void SendCurrentOutput(wl_resource* resource) const;
  static void GetCurrentOutput(wl_client* client, wl_resource* resource);
  static void CreateShellSurface(wl_client* client, wl_resource* shell,
                                 uint32_t id, wl_resource* surface_resource);
  static void OnShellWlSurfaceDestroy(wl_listener* listener, void* data);
  static void DestroyShellSurface(wl_resource* resource);
  ShellSurface* FindShellSurface(wlr_surface* surface) const;
  static void SetShellOutput(wl_client* client, wl_resource* resource,
                             wl_resource* output_resource);
  static void SetShellPosition(wl_client* client, wl_resource* resource,
                               int32_t x, int32_t y);
  static void SetSkipTaskbar(wl_client* client, wl_resource* resource,
                             uint32_t skip);
  static void SetSkipSwitcher(wl_client* client, wl_resource* resource,
                              uint32_t skip);
  static void SetShellProperty(wl_client* client, wl_resource* resource,
                               uint32_t property, uint32_t value);
  static void SetShellRole(wl_client* client, wl_resource* resource,
                           uint32_t role);
  static void SetPanelAutoHide(wl_client* client, wl_resource* resource,
                               uint32_t hide);
  static void OpenUnderCursor(wl_client* client, wl_resource* resource,
                              int32_t x, int32_t y);
  static void SetPanelTakesFocus(wl_client* client, wl_resource* resource,
                                 uint32_t takes_focus);
  static void GrabKeyboard(wl_client* client, wl_resource* resource,
                           wl_resource* seat_resource);
  static void SetShellIcon(wl_client* client, wl_resource* resource,
                           const char* icon_name);
  static void ActivateShellSurface(wl_client* client, wl_resource* resource);
  void Activate(ShellSurface* state) const;
  void ApplyShellSurface(ShellSurface* state) const;

  static void BindWindowManagement(wl_client* client, void* data,
                                   uint32_t version, uint32_t id);
  static void RemoveWindowManagement(wl_resource* resource);
  static void ShowDesktop(wl_client* client, wl_resource* resource,
                          uint32_t state);
  static void CreateWindowResource(wl_client* client, wl_resource* management,
                                   uint32_t id, const char* uuid);
  static void SetWindowState(wl_client* client, wl_resource* resource,
                             uint32_t flags, uint32_t state);
  static void NoopStartupGeometry(wl_client* client, wl_resource* resource,
                                  wl_resource* entry, uint32_t x, uint32_t y,
                                  uint32_t width, uint32_t height);
  static void NoopMinimizedGeometry(wl_client* client, wl_resource* resource,
                                    wl_resource* panel, uint32_t x, uint32_t y,
                                    uint32_t width, uint32_t height);
  static void NoopUnsetMinimizedGeometry(wl_client* client,
                                         wl_resource* resource,
                                         wl_resource* panel);
  static Window* WindowFromResource(wl_resource* resource);
  static void CloseWindow(wl_client* client, wl_resource* resource);
  static void MoveWindow(wl_client* client, wl_resource* resource);
  static void ResizeWindow(wl_client* client, wl_resource* resource);
  static void GetWindowIcon(wl_client* client, wl_resource* resource,
                            int32_t fd);
  static void NoopDesktopId(wl_client* client, wl_resource* resource,
                            const char* id);
  static void EnterVirtualDesktop(wl_client* client, wl_resource* resource,
                                  const char* id);
  static void NoopWindowRequest(wl_client* client, wl_resource* resource);
  static void SendWindowToOutput(wl_client* client, wl_resource* resource,
                                 wl_resource* output_resource);
  static void HighlightWindow(wl_client* client, wl_resource* resource);
  static void UnsetHighlightWindow(wl_client* client, wl_resource* resource);
  static void DestroyWindowResource(wl_resource* resource);
  void AddWindowResource(wl_client* client, wl_resource* management,
                         uint32_t id, Window* window);
  uint32_t WindowState(Window* window) const;
  void SendWindow(wl_resource* resource, Window* window) const;
  void MapToplevel(wlr_surface* surface);
  void UnmapToplevel(wlr_surface* surface);
  void UpdateToplevel(wlr_surface* surface);
  void UpdateToplevelParent(wlr_surface* surface, wlr_surface* parent);
  Window* FindWindow(wlr_surface* surface) const;
  Window* FindWindow(const char* uuid) const;
  void SendStackingOrder(wl_resource* only = nullptr) const;
  void SetHighlight(Window* window);

  static void BindBlurManager(wl_client* client, void* data, uint32_t version,
                              uint32_t id);
  static void CreateBlur(wl_client* client, wl_resource* manager_resource,
                         uint32_t id, wl_resource* surface_resource);
  static void SetBlurRegion(wl_client* client, wl_resource* resource,
                            wl_resource* region_resource);
  static void SetBlurLevel(wl_client* client, wl_resource* resource,
                           uint32_t level);
  static void OnBlurCommit(wl_listener* listener, void* data);
  void ApplyBlur(Blur* blur) const;
  static void OnBlurSurfaceDestroy(wl_listener* listener, void* data);
  static void DestroyBlur(wl_resource* resource);

  core::CompositorPrivate* compositor;
  wl_display* display = nullptr;
  wlr_seat* seat = nullptr;
  wlr_output_layout* output_layout = nullptr;
  wl_global* blur_global = nullptr;
  wl_global* output_global = nullptr;
  wl_global* shell_global = nullptr;
  wl_global* window_global = nullptr;
  uint64_t next_window_id = 1;
  bool showing_desktop = false;
  Window* highlight_window = nullptr;
  std::vector<std::unique_ptr<Output>> outputs;
  std::vector<wl_resource*> output_management_resources;
  std::vector<ShellSurface*> shell_surfaces;
  std::vector<std::unique_ptr<Window>> windows;
  std::vector<wl_resource*> window_management_resources;
  std::vector<wlr_surface*> show_desktop_surfaces;
  std::vector<Blur*> blurs;
};

}  // namespace protocol
}  // namespace flakewm

#endif  // SRC_PROTOCOL_UKUI_UKUI_PROTOCOL_MANAGER_INTERNAL_H_
