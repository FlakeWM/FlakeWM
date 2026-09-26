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

#include <algorithm>

#include "protocol/ukui-shell-protocol.h"
#include "src/core/compositor_private/compositor_private.h"
#include "src/protocol/ukui/ukui_protocol_manager_internal.h"

namespace flakewm {
namespace protocol {
namespace {
using ukui_internal::kMaximumCoordinate;
using ukui_internal::kShellVersion;
using ukui_internal::Safe;
}  // namespace

void UkuiProtocolManager::Impl::BindShell(wl_client* client, void* data,
                                          uint32_t version, uint32_t id) {
  static const struct ukui_shell_interface implementation = {
      .create_surface = CreateShellSurface,
      .get_current_output = GetCurrentOutput,
  };
  auto* manager = static_cast<Impl*>(data);
  wl_resource* resource = wl_resource_create(
      client, &ukui_shell_interface,
      static_cast<int>(std::min(version, kShellVersion)), id);
  if (resource == nullptr) {
    wl_client_post_no_memory(client);
    return;
  }
  wl_resource_set_implementation(resource, &implementation, manager, nullptr);
  manager->SendCurrentOutput(resource);
}

void UkuiProtocolManager::Impl::SendCurrentOutput(wl_resource* resource) const {
  if (wl_resource_get_version(resource) < 2) {
    return;
  }
  wlr_output* output = wlr_output_layout_output_at(
      output_layout, compositor->cursor_->x, compositor->cursor_->y);
  if (output == nullptr) {
    output = wlr_output_layout_get_center_output(output_layout);
  }
  ukui_shell_send_current_output(resource,
                                 output == nullptr ? "" : Safe(output->name),
                                 seat == nullptr ? "" : Safe(seat->name));
  ukui_shell_send_done(resource);
}

void UkuiProtocolManager::Impl::GetCurrentOutput(wl_client*,
                                                 wl_resource* resource) {
  auto* manager = static_cast<Impl*>(wl_resource_get_user_data(resource));
  manager->SendCurrentOutput(resource);
}

void UkuiProtocolManager::Impl::CreateShellSurface(
    wl_client* client, wl_resource* shell, uint32_t id,
    wl_resource* surface_resource) {
  static const struct ukui_surface_interface implementation = {
      .destroy = DestroyResourceRequest,
      .set_output = SetShellOutput,
      .set_position = SetShellPosition,
      .set_skip_taskbar = SetSkipTaskbar,
      .set_skip_switcher = SetSkipSwitcher,
      .set_property = SetShellProperty,
      .set_role = SetShellRole,
      .set_panel_auto_hide = SetPanelAutoHide,
      .open_under_cursor = OpenUnderCursor,
      .set_panel_takes_focus = SetPanelTakesFocus,
      .grab_keyboard = GrabKeyboard,
      .set_icon = SetShellIcon,
      .activate = ActivateShellSurface,
  };
  auto* manager = static_cast<Impl*>(wl_resource_get_user_data(shell));
  wlr_surface* surface = wlr_surface_from_resource(surface_resource);
  if (surface == nullptr) {
    return;
  }
  if (manager->FindShellSurface(surface) != nullptr) {
    wl_client_post_implementation_error(
        client, "wl_surface already has a UKUI surface");
    return;
  }
  wl_resource* resource = wl_resource_create(
      client, &ukui_surface_interface, wl_resource_get_version(shell), id);
  if (resource == nullptr) {
    wl_client_post_no_memory(client);
    return;
  }
  auto* state = new ShellSurface{
      .manager = manager, .resource = resource, .surface = surface};
  state->surface_destroy.notify = OnShellWlSurfaceDestroy;
  wl_signal_add(&surface->events.destroy, &state->surface_destroy);
  manager->shell_surfaces.push_back(state);
  wl_resource_set_implementation(resource, &implementation, state,
                                 DestroyShellSurface);
}

void UkuiProtocolManager::Impl::OnShellWlSurfaceDestroy(wl_listener* listener,
                                                        void*) {
  ShellSurface* state = wl_container_of(listener, state, surface_destroy);
  state->surface = nullptr;
  wl_list_remove(&state->surface_destroy.link);
}

void UkuiProtocolManager::Impl::DestroyShellSurface(wl_resource* resource) {
  auto* state = static_cast<ShellSurface*>(wl_resource_get_user_data(resource));
  if (state == nullptr) {
    return;
  }
  if (state->surface != nullptr) {
    core::CompositorPrivate::Toplevel* toplevel =
        state->manager->compositor->ToplevelForSurface(state->surface);
    if (toplevel != nullptr && toplevel->scene_tree != nullptr &&
        toplevel->scene_tree->node.parent !=
            state->manager->compositor->toplevel_tree_) {
      wlr_scene_node_reparent(&toplevel->scene_tree->node,
                              state->manager->compositor->toplevel_tree_);
    }
    wl_list_remove(&state->surface_destroy.link);
  }
  std::erase(state->manager->shell_surfaces, state);
  delete state;
}

UkuiProtocolManager::Impl::ShellSurface*
UkuiProtocolManager::Impl::FindShellSurface(wlr_surface* surface) const {
  if (surface == nullptr) {
    return nullptr;
  }
  const wlr_surface* root = wlr_surface_get_root_surface(surface);
  auto it = std::find_if(
      shell_surfaces.begin(), shell_surfaces.end(),
      [root](ShellSurface* state) {
        return state->surface != nullptr &&
               wlr_surface_get_root_surface(state->surface) == root;
      });
  return it == shell_surfaces.end() ? nullptr : *it;
}

// The callback parameter order is fixed by the generated Wayland ABI.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void UkuiProtocolManager::Impl::SetShellOutput(
    wl_client*,
    wl_resource* resource,  // NOLINT(bugprone-easily-swappable-parameters)
    wl_resource* output_resource) {
  auto* state = static_cast<ShellSurface*>(wl_resource_get_user_data(resource));
  state->output = output_resource == nullptr
                      ? nullptr
                      : wlr_output_from_resource(output_resource);
}

void UkuiProtocolManager::Impl::SetShellPosition(wl_client*,
                                                 wl_resource* resource,
                                                 int32_t x, int32_t y) {
  auto* state = static_cast<ShellSurface*>(wl_resource_get_user_data(resource));
  state->x = std::clamp(x, -kMaximumCoordinate, kMaximumCoordinate);
  state->y = std::clamp(y, -kMaximumCoordinate, kMaximumCoordinate);
  state->position_set = true;
  state->manager->ApplyShellSurface(state);
}

void UkuiProtocolManager::Impl::SetSkipTaskbar(wl_client*,
                                               wl_resource* resource,
                                               uint32_t skip) {
  auto* state = static_cast<ShellSurface*>(wl_resource_get_user_data(resource));
  state->skip_taskbar = skip != 0;
  state->manager->UpdateToplevel(state->surface);
}

void UkuiProtocolManager::Impl::SetSkipSwitcher(wl_client*,
                                                wl_resource* resource,
                                                uint32_t skip) {
  auto* state = static_cast<ShellSurface*>(wl_resource_get_user_data(resource));
  state->skip_switcher = skip != 0;
  state->manager->UpdateToplevel(state->surface);
}

// The callback parameter order is fixed by the generated Wayland ABI.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void UkuiProtocolManager::Impl::SetShellProperty(
    wl_client*, wl_resource* resource,
    uint32_t property,  // NOLINT(bugprone-easily-swappable-parameters)
    uint32_t value) {
  auto* state = static_cast<ShellSurface*>(wl_resource_get_user_data(resource));
  if (state->surface == nullptr ||
      property != UKUI_SURFACE_PROPERTY_NO_TITLEBAR) {
    return;
  }
  core::CompositorPrivate::Toplevel* toplevel =
      state->manager->compositor->ToplevelForSurface(state->surface);
  if (toplevel == nullptr) {
    return;
  }
  if (value != 0 && toplevel->ssd != nullptr) {
    state->removed_ssd = true;
    state->manager->compositor->SetSsdEnabled(toplevel, false);
  } else if (value == 0 && state->removed_ssd && toplevel->ssd == nullptr) {
    state->manager->compositor->SetSsdEnabled(toplevel, true);
    state->removed_ssd = false;
  }
}

void UkuiProtocolManager::Impl::SetShellRole(wl_client*, wl_resource* resource,
                                             uint32_t role) {
  auto* state = static_cast<ShellSurface*>(wl_resource_get_user_data(resource));
  if (!ukui_surface_role_is_valid(role, wl_resource_get_version(resource))) {
    return;
  }
  state->role = role;
  state->manager->ApplyShellSurface(state);
}

void UkuiProtocolManager::Impl::SetPanelAutoHide(wl_client*,
                                                 wl_resource* resource,
                                                 uint32_t hide) {
  auto* state = static_cast<ShellSurface*>(wl_resource_get_user_data(resource));
  state->panel_auto_hide = hide != 0;
  state->manager->ApplyShellSurface(state);
}

// Coordinates are ordered by the protocol definition.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void UkuiProtocolManager::Impl::OpenUnderCursor(
    wl_client*, wl_resource* resource,
    int32_t x,  // NOLINT(bugprone-easily-swappable-parameters)
    int32_t y) {
  auto* state = static_cast<ShellSurface*>(wl_resource_get_user_data(resource));
  state->x = std::clamp(
      static_cast<int32_t>(state->manager->compositor->cursor_->x) + x,
      -kMaximumCoordinate, kMaximumCoordinate);
  state->y = std::clamp(
      static_cast<int32_t>(state->manager->compositor->cursor_->y) + y,
      -kMaximumCoordinate, kMaximumCoordinate);
  state->position_set = true;
  state->manager->ApplyShellSurface(state);
}

void UkuiProtocolManager::Impl::SetPanelTakesFocus(wl_client*,
                                                   wl_resource* resource,
                                                   uint32_t takes_focus) {
  auto* state = static_cast<ShellSurface*>(wl_resource_get_user_data(resource));
  state->panel_takes_focus = takes_focus != 0;
}

// The callback parameter order is fixed by the generated Wayland ABI.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void UkuiProtocolManager::Impl::GrabKeyboard(wl_client*, wl_resource* resource,
                                             wl_resource* seat_resource) {
  auto* state = static_cast<ShellSurface*>(wl_resource_get_user_data(resource));
  if (seat_resource != nullptr) {
    wlr_seat_client* seat_client = wlr_seat_client_from_resource(seat_resource);
    if (seat_client == nullptr || seat_client->seat != state->manager->seat) {
      return;
    }
  }
  state->manager->Activate(state);
}

void UkuiProtocolManager::Impl::SetShellIcon(wl_client*, wl_resource* resource,
                                             const char* icon_name) {
  auto* state = static_cast<ShellSurface*>(wl_resource_get_user_data(resource));
  state->icon_name = Safe(icon_name);
  core::CompositorPrivate::Toplevel* toplevel =
      state->manager->compositor->ToplevelForSurface(state->surface);
  if (toplevel != nullptr && toplevel->ssd != nullptr) {
    toplevel->ssd->SetAppId(state->icon_name.empty() ? Safe(toplevel->AppId())
                                                     : state->icon_name);
  }
}

void UkuiProtocolManager::Impl::ActivateShellSurface(wl_client*,
                                                     wl_resource* resource) {
  auto* state = static_cast<ShellSurface*>(wl_resource_get_user_data(resource));
  state->manager->Activate(state);
}

void UkuiProtocolManager::Impl::Activate(ShellSurface* state) const {
  if (state == nullptr || state->surface == nullptr ||
      !state->surface->mapped) {
    return;
  }
  compositor->FocusToplevel(compositor->ToplevelForSurface(state->surface));
}

void UkuiProtocolManager::Impl::ApplyShellSurface(ShellSurface* state) const {
  if (state == nullptr || state->surface == nullptr) {
    return;
  }
  core::CompositorPrivate::Toplevel* toplevel =
      compositor->ToplevelForSurface(state->surface);
  if (toplevel == nullptr || toplevel->scene_tree == nullptr) {
    return;
  }
  wlr_scene_tree* parent = compositor->toplevel_tree_;
  switch (state->role) {
    case UKUI_SURFACE_ROLE_DESKTOP:
      parent =
          compositor->shell_layer_trees_[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND];
      break;
    case UKUI_SURFACE_ROLE_PANEL:
      parent = compositor->shell_layer_trees_[ZWLR_LAYER_SHELL_V1_LAYER_TOP];
      break;
    case UKUI_SURFACE_ROLE_ONSCREENDISPLAY:
    case UKUI_SURFACE_ROLE_NOTIFICATION:
    case UKUI_SURFACE_ROLE_TOOLTIP:
    case UKUI_SURFACE_ROLE_CRITICALNOTIFICATION:
    case UKUI_SURFACE_ROLE_APPLETPOPUP:
    case UKUI_SURFACE_ROLE_SCREENLOCK:
    case UKUI_SURFACE_ROLE_WATERMARK:
    case UKUI_SURFACE_ROLE_SYSTEMWINDOW:
    case UKUI_SURFACE_ROLE_INPUTPANEL:
    case UKUI_SURFACE_ROLE_LOGOUT:
    case UKUI_SURFACE_ROLE_SCREENLOCKNOTIFICATION:
    case UKUI_SURFACE_ROLE_SWITCHER:
    case UKUI_SURFACE_ROLE_AUTHENTICATION:
      parent =
          compositor->shell_layer_trees_[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY];
      break;
    default:
      break;
  }
  if (parent != nullptr && toplevel->scene_tree->node.parent != parent) {
    wlr_scene_node_reparent(&toplevel->scene_tree->node, parent);
  }
  if (state->position_set) {
    toplevel->position_requested = true;
    wlr_scene_node_set_position(&toplevel->scene_tree->node, state->x,
                                state->y);
    if (wl_resource_get_version(state->resource) >= 3) {
      ukui_surface_send_position(state->resource, state->x, state->y);
    }
  }
}

}  // namespace protocol
}  // namespace flakewm
