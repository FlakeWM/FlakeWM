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

#include <unistd.h>

#include <algorithm>
#include <memory>
#include <string>

#include "protocol/ukui-window-management-protocol.h"
#include "src/core/compositor_private/compositor_private.h"
#include "src/protocol/protocol_manager/protocol_manager.h"
#include "src/protocol/ukui/ukui_protocol_manager_internal.h"

namespace flakewm {
namespace protocol {
namespace {
using ukui_internal::kDesktopId;
using ukui_internal::kWindowVersion;
using ukui_internal::Safe;
}  // namespace

void UkuiProtocolManager::Impl::BindWindowManagement(wl_client* client,
                                                     void* data,
                                                     uint32_t version,
                                                     uint32_t id) {
  static const struct ukui_window_management_interface implementation = {
      .show_desktop = ShowDesktop,
      .create_window = CreateWindowResource,
  };
  auto* manager = static_cast<Impl*>(data);
  wl_resource* resource = wl_resource_create(
      client, &ukui_window_management_interface,
      static_cast<int>(std::min(version, kWindowVersion)), id);
  if (resource == nullptr) {
    wl_client_post_no_memory(client);
    return;
  }
  manager->window_management_resources.push_back(resource);
  wl_resource_set_implementation(resource, &implementation, manager,
                                 RemoveWindowManagement);
  ukui_window_management_send_show_desktop_changed(
      resource, manager->showing_desktop
                    ? UKUI_WINDOW_MANAGEMENT_SHOW_DESKTOP_ENABLED
                    : UKUI_WINDOW_MANAGEMENT_SHOW_DESKTOP_DISABLED);
  for (const std::unique_ptr<Window>& window : manager->windows) {
    ukui_window_management_send_window_created(resource, window->uuid.c_str());
  }
  manager->SendStackingOrder(resource);
}

void UkuiProtocolManager::Impl::RemoveWindowManagement(wl_resource* resource) {
  auto* manager = static_cast<Impl*>(wl_resource_get_user_data(resource));
  if (manager != nullptr) {
    std::erase(manager->window_management_resources, resource);
  }
}

void UkuiProtocolManager::Impl::ShowDesktop(wl_client*, wl_resource* resource,
                                            uint32_t state) {
  auto* manager = static_cast<Impl*>(wl_resource_get_user_data(resource));
  const bool show = state == UKUI_WINDOW_MANAGEMENT_SHOW_DESKTOP_ENABLED;
  if (manager->showing_desktop == show) {
    return;
  }
  manager->showing_desktop = show;
  if (show) {
    manager->show_desktop_surfaces.clear();
    for (const std::unique_ptr<Window>& window : manager->windows) {
      core::CompositorPrivate::Toplevel* toplevel =
          manager->compositor->ToplevelForSurface(window->surface);
      if (toplevel != nullptr && !toplevel->minimized) {
        manager->show_desktop_surfaces.push_back(window->surface);
        manager->compositor->protocol_manager_->RequestMinimize(window->surface,
                                                                true);
      }
    }
  } else {
    for (wlr_surface* surface : manager->show_desktop_surfaces) {
      manager->compositor->protocol_manager_->RequestMinimize(surface, false);
      manager->UpdateToplevel(surface);
    }
    manager->show_desktop_surfaces.clear();
  }
  for (wl_resource* management : manager->window_management_resources) {
    ukui_window_management_send_show_desktop_changed(management, state);
  }
}

void UkuiProtocolManager::Impl::CreateWindowResource(wl_client* client,
                                                     wl_resource* management,
                                                     uint32_t id,
                                                     const char* uuid) {
  auto* manager = static_cast<Impl*>(wl_resource_get_user_data(management));
  Window* window = manager->FindWindow(uuid);
  manager->AddWindowResource(client, management, id, window);
}

void UkuiProtocolManager::Impl::SetWindowState(wl_client*,
                                               wl_resource* resource,
                                               uint32_t flags, uint32_t state) {
  auto* window = static_cast<Window*>(wl_resource_get_user_data(resource));
  if (window == nullptr || window->surface == nullptr) {
    return;
  }
  ProtocolManager* protocols =
      window->manager->compositor->protocol_manager_.get();
  if ((flags & UKUI_WINDOW_STATE_ACTIVE) != 0 &&
      (state & UKUI_WINDOW_STATE_ACTIVE) != 0) {
    protocols->RequestActivate(window->surface, window->manager->seat);
  }
  if ((flags & UKUI_WINDOW_STATE_MINIMIZED) != 0) {
    protocols->RequestMinimize(window->surface,
                               (state & UKUI_WINDOW_STATE_MINIMIZED) != 0);
  }
  if ((flags & UKUI_WINDOW_STATE_MAXIMIZED) != 0) {
    protocols->RequestMaximize(window->surface,
                               (state & UKUI_WINDOW_STATE_MAXIMIZED) != 0);
  }
  if ((flags & UKUI_WINDOW_STATE_FULLSCREEN) != 0) {
    protocols->RequestFullscreen(
        window->surface, (state & UKUI_WINDOW_STATE_FULLSCREEN) != 0, nullptr);
  }
  constexpr uint32_t kExtraMask =
      UKUI_WINDOW_STATE_KEEP_ABOVE | UKUI_WINDOW_STATE_KEEP_BELOW |
      UKUI_WINDOW_STATE_DEMANDS_ATTENTION | UKUI_WINDOW_STATE_SKIPTASKBAR |
      UKUI_WINDOW_STATE_SKIPSWITCHER | UKUI_WINDOW_STATE_MODALITY;
  window->extra_state = (window->extra_state & ~(flags & kExtraMask)) |
                        (state & flags & kExtraMask);
  core::CompositorPrivate::Toplevel* toplevel =
      window->manager->compositor->ToplevelForSurface(window->surface);
  if (toplevel != nullptr && toplevel->scene_tree != nullptr) {
    if ((flags & UKUI_WINDOW_STATE_KEEP_ABOVE) != 0 &&
        (state & UKUI_WINDOW_STATE_KEEP_ABOVE) != 0) {
      wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
    } else if ((flags & UKUI_WINDOW_STATE_KEEP_BELOW) != 0 &&
               (state & UKUI_WINDOW_STATE_KEEP_BELOW) != 0) {
      wlr_scene_node_lower_to_bottom(&toplevel->scene_tree->node);
    }
  }
  window->manager->UpdateToplevel(window->surface);
}

void UkuiProtocolManager::Impl::NoopStartupGeometry(wl_client*, wl_resource*,
                                                    wl_resource*, uint32_t,
                                                    uint32_t, uint32_t,
                                                    uint32_t) {}
void UkuiProtocolManager::Impl::NoopMinimizedGeometry(wl_client*, wl_resource*,
                                                      wl_resource*, uint32_t,
                                                      uint32_t, uint32_t,
                                                      uint32_t) {}
void UkuiProtocolManager::Impl::NoopUnsetMinimizedGeometry(wl_client*,
                                                           wl_resource*,
                                                           wl_resource*) {}

UkuiProtocolManager::Impl::Window*
UkuiProtocolManager::Impl::WindowFromResource(wl_resource* resource) {
  return static_cast<Window*>(wl_resource_get_user_data(resource));
}

void UkuiProtocolManager::Impl::CloseWindow(wl_client*, wl_resource* resource) {
  if (Window* window = WindowFromResource(resource); window != nullptr) {
    window->manager->compositor->protocol_manager_->RequestClose(
        window->surface);
  }
}

void UkuiProtocolManager::Impl::MoveWindow(wl_client*, wl_resource* resource) {
  Window* window = WindowFromResource(resource);
  if (window == nullptr) {
    return;
  }
  core::CompositorPrivate::Toplevel* toplevel =
      window->manager->compositor->ToplevelForSurface(window->surface);
  window->manager->compositor->BeginInteractive(
      toplevel, core::CompositorPrivate::CursorMode::kMove, 0);
}

void UkuiProtocolManager::Impl::ResizeWindow(wl_client*,
                                             wl_resource* resource) {
  Window* window = WindowFromResource(resource);
  if (window == nullptr) {
    return;
  }
  core::CompositorPrivate::Toplevel* toplevel =
      window->manager->compositor->ToplevelForSurface(window->surface);
  window->manager->compositor->BeginInteractive(
      toplevel, core::CompositorPrivate::CursorMode::kResize,
      WLR_EDGE_RIGHT | WLR_EDGE_BOTTOM);
}

void UkuiProtocolManager::Impl::GetWindowIcon(wl_client*, wl_resource*,
                                              int32_t fd) {
  close(fd);
}
void UkuiProtocolManager::Impl::NoopDesktopId(wl_client*, wl_resource*,
                                              const char*) {}
void UkuiProtocolManager::Impl::NoopWindowRequest(wl_client*, wl_resource*) {}

// The callback parameter order is fixed by the generated Wayland ABI.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void UkuiProtocolManager::Impl::SendWindowToOutput(
    wl_client*,
    wl_resource* resource,  // NOLINT(bugprone-easily-swappable-parameters)
    wl_resource* output_resource) {
  Window* window = WindowFromResource(resource);
  wlr_output* output = output_resource == nullptr
                           ? nullptr
                           : wlr_output_from_resource(output_resource);
  if (window == nullptr || output == nullptr) {
    return;
  }
  core::CompositorPrivate::Toplevel* toplevel =
      window->manager->compositor->ToplevelForSurface(window->surface);
  if (toplevel == nullptr || toplevel->scene_tree == nullptr) {
    return;
  }
  const wlr_box target = window->manager->compositor->UsableOutputBox(output);
  const wlr_box frame = toplevel->FrameGeometry();
  wlr_scene_node_set_position(&toplevel->scene_tree->node, target.x - frame.x,
                              target.y - frame.y);
  window->manager->UpdateToplevel(window->surface);
}

void UkuiProtocolManager::Impl::HighlightWindow(wl_client*,
                                                wl_resource* resource) {
  Window* window = WindowFromResource(resource);
  if (window == nullptr || window->manager->highlight_window == window) {
    return;
  }
  window->manager->SetHighlight(window);
}

void UkuiProtocolManager::Impl::UnsetHighlightWindow(wl_client*,
                                                     wl_resource* resource) {
  Window* window = WindowFromResource(resource);
  if (window != nullptr && window->manager->highlight_window == window) {
    window->manager->SetHighlight(nullptr);
  }
}

void UkuiProtocolManager::Impl::DestroyWindowResource(wl_resource* resource) {
  Window* window = WindowFromResource(resource);
  if (window != nullptr) {
    std::erase(window->resources, resource);
  }
}

void UkuiProtocolManager::Impl::AddWindowResource(wl_client* client,
                                                  wl_resource* management,
                                                  uint32_t id, Window* window) {
  static const struct ukui_window_interface implementation = {
      .set_state = SetWindowState,
      .set_startup_geometry = NoopStartupGeometry,
      .set_minimized_geometry = NoopMinimizedGeometry,
      .unset_minimized_geometry = NoopUnsetMinimizedGeometry,
      .close = CloseWindow,
      .request_move = MoveWindow,
      .request_resize = ResizeWindow,
      .destroy = DestroyResourceRequest,
      .get_icon = GetWindowIcon,
      .request_enter_virtual_desktop = NoopDesktopId,
      .request_enter_new_virtual_desktop = NoopWindowRequest,
      .request_leave_virtual_desktop = NoopDesktopId,
      .request_enter_activity = NoopDesktopId,
      .request_leave_activity = NoopDesktopId,
      .send_to_output = SendWindowToOutput,
      .highlight = HighlightWindow,
      .unset_highlight = UnsetHighlightWindow,
  };
  wl_resource* resource = wl_resource_create(
      client, &ukui_window_interface, wl_resource_get_version(management), id);
  if (resource == nullptr) {
    wl_client_post_no_memory(client);
    return;
  }
  wl_resource_set_implementation(resource, &implementation, window,
                                 DestroyWindowResource);
  if (window == nullptr) {
    ukui_window_send_unmapped(resource);
    ukui_window_send_initial_state(resource);
    return;
  }
  window->resources.push_back(resource);
  SendWindow(resource, window);
  ukui_window_send_initial_state(resource);
}

uint32_t UkuiProtocolManager::Impl::WindowState(Window* window) const {
  core::CompositorPrivate::Toplevel* toplevel =
      compositor->ToplevelForSurface(window->surface);
  if (toplevel == nullptr) {
    return window->extra_state;
  }
  uint32_t state = window->extra_state | UKUI_WINDOW_STATE_ON_ALL_DESKTOPS |
                   UKUI_WINDOW_STATE_CLOSEABLE | UKUI_WINDOW_STATE_MOVABLE |
                   UKUI_WINDOW_STATE_RESIZABLE | UKUI_WINDOW_STATE_ACCEPT_FOCUS;
  if (seat != nullptr &&
      seat->keyboard_state.focused_surface == window->surface) {
    state |= UKUI_WINDOW_STATE_ACTIVE;
  }
  if (toplevel->minimized) state |= UKUI_WINDOW_STATE_MINIMIZED;
  if (toplevel->maximized) state |= UKUI_WINDOW_STATE_MAXIMIZED;
  if (toplevel->RequestedFullscreen()) state |= UKUI_WINDOW_STATE_FULLSCREEN;
  if (toplevel->CanMinimize()) state |= UKUI_WINDOW_STATE_MINIMIZABLE;
  if (toplevel->CanMaximize()) state |= UKUI_WINDOW_STATE_MAXIMIZABLE;
  state |= UKUI_WINDOW_STATE_FULLSCREENABLE;
  if (ShellSurface* shell = FindShellSurface(window->surface);
      shell != nullptr) {
    if (shell->skip_taskbar) state |= UKUI_WINDOW_STATE_SKIPTASKBAR;
    if (shell->skip_switcher) state |= UKUI_WINDOW_STATE_SKIPSWITCHER;
  }
  return state;
}

void UkuiProtocolManager::Impl::SendWindow(wl_resource* resource,
                                           Window* window) const {
  core::CompositorPrivate::Toplevel* toplevel =
      compositor->ToplevelForSurface(window->surface);
  if (toplevel == nullptr) {
    return;
  }
  ukui_window_send_title_changed(resource, Safe(toplevel->Title()));
  ukui_window_send_app_id_changed(resource, Safe(toplevel->AppId()));
  ukui_window_send_state_changed(resource, WindowState(window));
  pid_t pid = 0;
  wl_client_get_credentials(wl_resource_get_client(window->surface->resource),
                            &pid, nullptr, nullptr);
  if (pid > 0) {
    ukui_window_send_pid_changed(resource, static_cast<uint32_t>(pid));
  }
  const wlr_box geometry = toplevel->FrameGeometry();
  const int32_t x = toplevel->scene_tree == nullptr
                        ? geometry.x
                        : toplevel->scene_tree->node.x + geometry.x;
  const int32_t y = toplevel->scene_tree == nullptr
                        ? geometry.y
                        : toplevel->scene_tree->node.y + geometry.y;
  ukui_window_send_geometry(resource, x, y, std::max(geometry.width, 0),
                            std::max(geometry.height, 0));
  ukui_window_send_themed_icon_name_changed(resource, Safe(toplevel->AppId()));
  ukui_window_send_virtual_desktop_entered(resource, kDesktopId);
  Window* parent = window->parent;
  wl_resource* parent_resource = nullptr;
  if (parent != nullptr) {
    wl_client* client = wl_resource_get_client(resource);
    auto it = std::find_if(parent->resources.begin(), parent->resources.end(),
                           [client](wl_resource* candidate) {
                             return wl_resource_get_client(candidate) == client;
                           });
    if (it != parent->resources.end()) parent_resource = *it;
  }
  ukui_window_send_parent_window(resource, parent_resource);
}

void UkuiProtocolManager::Impl::MapToplevel(wlr_surface* surface) {
  if (surface == nullptr || FindWindow(surface) != nullptr) {
    return;
  }
  auto window = std::make_unique<Window>();
  window->manager = this;
  window->surface = surface;
  window->uuid = "flakewm-ukui-window-" + std::to_string(next_window_id++);
  core::CompositorPrivate::Toplevel* toplevel =
      compositor->ToplevelForSurface(surface);
  if (toplevel != nullptr && toplevel->handle != nullptr &&
      toplevel->handle->parent != nullptr) {
    window->parent = FindWindow(toplevel->handle->parent->base->surface);
  }
  Window* raw = window.get();
  windows.push_back(std::move(window));
  for (wl_resource* management : window_management_resources) {
    ukui_window_management_send_window_created(management, raw->uuid.c_str());
  }
  SendStackingOrder();
}

void UkuiProtocolManager::Impl::UnmapToplevel(wlr_surface* surface) {
  Window* window = FindWindow(surface);
  if (window == nullptr) {
    return;
  }
  if (highlight_window == window) SetHighlight(nullptr);
  std::erase(show_desktop_surfaces, surface);
  for (const std::unique_ptr<Window>& candidate : windows) {
    if (candidate->parent != window) {
      continue;
    }
    candidate->parent = nullptr;
    for (wl_resource* resource : candidate->resources) {
      ukui_window_send_parent_window(resource, nullptr);
    }
  }
  for (wl_resource* resource : window->resources) {
    ukui_window_send_unmapped(resource);
    wl_resource_set_user_data(resource, nullptr);
  }
  window->resources.clear();
  std::erase_if(windows,
                [window](const auto& item) { return item.get() == window; });
  SendStackingOrder();
}

void UkuiProtocolManager::Impl::UpdateToplevel(wlr_surface* surface) {
  if (ShellSurface* shell = FindShellSurface(surface); shell != nullptr) {
    ApplyShellSurface(shell);
  }
  Window* window = FindWindow(surface);
  if (window == nullptr) {
    return;
  }
  for (wl_resource* resource : window->resources) {
    SendWindow(resource, window);
  }
  SendStackingOrder();
}

// Child and parent are deliberately adjacent in this lifecycle callback.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void UkuiProtocolManager::Impl::UpdateToplevelParent(wlr_surface* surface,
                                                     wlr_surface* parent) {
  Window* window = FindWindow(surface);
  if (window == nullptr) {
    return;
  }
  window->parent = FindWindow(parent);
  for (wl_resource* resource : window->resources) {
    SendWindow(resource, window);
  }
}

UkuiProtocolManager::Impl::Window* UkuiProtocolManager::Impl::FindWindow(
    wlr_surface* surface) const {
  if (surface == nullptr) return nullptr;
  const wlr_surface* root = wlr_surface_get_root_surface(surface);
  auto it =
      std::find_if(windows.begin(), windows.end(), [root](const auto& item) {
        return item->surface != nullptr &&
               wlr_surface_get_root_surface(item->surface) == root;
      });
  return it == windows.end() ? nullptr : it->get();
}

UkuiProtocolManager::Impl::Window* UkuiProtocolManager::Impl::FindWindow(
    const char* uuid) const {
  auto it = std::find_if(
      windows.begin(), windows.end(),
      [uuid](const auto& item) { return item->uuid == Safe(uuid); });
  return it == windows.end() ? nullptr : it->get();
}

void UkuiProtocolManager::Impl::SendStackingOrder(wl_resource* only) const {
  std::string order;
  for (const std::unique_ptr<Window>& window : windows) {
    if (!order.empty()) order += ';';
    order += window->uuid;
  }
  if (only != nullptr) {
    ukui_window_management_send_stacking_order_changed(only, order.c_str());
    return;
  }
  for (wl_resource* resource : window_management_resources) {
    ukui_window_management_send_stacking_order_changed(resource, order.c_str());
  }
}

void UkuiProtocolManager::Impl::SetHighlight(Window* window) {
  highlight_window = window;
  for (const std::unique_ptr<Window>& candidate : windows) {
    core::CompositorPrivate::Toplevel* toplevel =
        compositor->ToplevelForSurface(candidate->surface);
    if (toplevel != nullptr && toplevel->scene_tree != nullptr) {
      wlr_scene_node_set_enabled(
          &toplevel->scene_tree->node,
          window == nullptr ? !toplevel->minimized : candidate.get() == window);
    }
  }
}

}  // namespace protocol
}  // namespace flakewm
