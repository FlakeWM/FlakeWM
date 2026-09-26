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

#include "src/protocol/kde/kde_protocol_manager.h"

#include <absl/log/absl_log.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "protocol/blur-protocol.h"
#include "protocol/kde-keystate-protocol.h"
#include "protocol/plasma-shell-protocol.h"
#include "protocol/plasma-virtual-desktop-protocol.h"
#include "protocol/plasma-window-management-protocol.h"
#include "src/core/compositor_private/compositor_private.h"
#include "src/protocol/blur_level.h"
#include "src/protocol/kde/kde_idle_manager.h"
#include "src/protocol/kde/kde_output_manager.h"
#include "src/protocol/protocol_manager/protocol_manager.h"
#include "src/render/backdrop_blur_renderer.h"
#include "src/render/blur_kernel.h"

namespace flakewm {
namespace protocol {
namespace {

constexpr uint32_t kKeyStateVersion = 4;
constexpr uint32_t kPlasmaShellVersion = 6;
constexpr uint32_t kVirtualDesktopVersion = 2;
constexpr uint32_t kPlasmaWindowVersion = 16;
constexpr uint32_t kBlurVersion = 1;
constexpr int32_t kMaximumSceneCoordinate =
    std::numeric_limits<int32_t>::max() / 4;
constexpr int kMaximumDesktopCount = 6;

const char* Safe(const char* value) { return value == nullptr ? "" : value; }

std::string DesktopId(int index) {
  return "flakewm-desktop-" + std::to_string(index + 1);
}

std::string DesktopName(int index) {
  return "Desktop " + std::to_string(index + 1);
}

std::optional<int> DesktopIndex(std::string_view id) {
  for (int index = 0; index < kMaximumDesktopCount; ++index) {
    if (id == DesktopId(index)) return index;
  }
  return std::nullopt;
}

}  // namespace

class KdeProtocolManager::Impl final {
 public:
  struct Keyboard;
  struct PlasmaSurface;
  struct PlasmaWindow;
  struct VirtualDesktop {
    Impl* manager;
    wl_resource* resource;
    int index;
  };
  struct Blur;
  struct ServerDecoration;
  struct NewServerDecorationListener;

  explicit Impl(core::CompositorPrivate* compositor) : compositor(compositor) {}

  ~Impl() {
    for (VirtualDesktop* desktop : desktop_resources) {
      wl_resource_set_user_data(desktop->resource, nullptr);
      delete desktop;
    }
    desktop_resources.clear();
    if (blur_global != nullptr) {
      wl_global_destroy(blur_global);
    }
    if (window_global != nullptr) {
      wl_global_destroy(window_global);
    }
    if (desktop_global != nullptr) {
      wl_global_destroy(desktop_global);
    }
    if (shell_global != nullptr) {
      wl_global_destroy(shell_global);
    }
    if (keystate_global != nullptr) {
      wl_global_destroy(keystate_global);
    }
    if (server_decoration != nullptr) {
      wl_list_remove(&new_server_decoration.listener.link);
    }
    for (ServerDecoration* decoration : server_decorations) {
      wl_list_remove(&decoration->mode.link);
      wl_list_remove(&decoration->destroy.link);
      delete decoration;
    }
    server_decorations.clear();
    kde_outputs.reset();
    kde_idle.reset();
    for (Keyboard* keyboard : keyboards) {
      wl_list_remove(&keyboard->key.link);
      wl_list_remove(&keyboard->modifiers.link);
      wl_list_remove(&keyboard->destroy.link);
      delete keyboard;
    }
    keyboards.clear();
  }

  bool Create(wl_display* new_display, wlr_backend* backend, wlr_seat* new_seat,
              wlr_output_layout* new_output_layout) {
    display = new_display;
    seat = new_seat;
    output_layout = new_output_layout;

    kde_idle = std::make_unique<KdeIdleManager>(display, seat);
    kde_outputs = std::make_unique<KdeOutputManager>(compositor, display,
                                                     backend, output_layout);
    keystate_global =
        wl_global_create(display, &org_kde_kwin_keystate_interface,
                         kKeyStateVersion, this, BindKeyState);
    shell_global = wl_global_create(display, &org_kde_plasma_shell_interface,
                                    kPlasmaShellVersion, this, BindShell);
    desktop_global = wl_global_create(
        display, &org_kde_plasma_virtual_desktop_management_interface,
        kVirtualDesktopVersion, this, BindDesktopManagement);
    window_global =
        wl_global_create(display, &org_kde_plasma_window_management_interface,
                         kPlasmaWindowVersion, this, BindWindowManagement);
    const bool blur_supported =
        compositor->backdrop_blur_renderer_ != nullptr &&
        compositor->backdrop_blur_renderer_->IsSupported();
    if (blur_supported) {
      blur_global =
          wl_global_create(display, &org_kde_kwin_blur_manager_interface,
                           kBlurVersion, this, BindBlurManager);
    } else {
      ABSL_LOG(WARNING) << "KDE blur is unavailable on the selected renderer";
    }
    server_decoration = wlr_server_decoration_manager_create(display);
    if (server_decoration != nullptr) {
      wlr_server_decoration_manager_set_default_mode(
          server_decoration, WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);
      new_server_decoration.manager = this;
      new_server_decoration.listener.notify = OnNewServerDecoration;
      wl_signal_add(&server_decoration->events.new_decoration,
                    &new_server_decoration.listener);
    }

    return kde_idle->IsValid() && kde_outputs->IsValid() &&
           keystate_global != nullptr && shell_global != nullptr &&
           desktop_global != nullptr && window_global != nullptr &&
           (!blur_supported || blur_global != nullptr) &&
           server_decoration != nullptr;
  }

  void AddInput(wlr_input_device* device) {
    if (device == nullptr || device->type != WLR_INPUT_DEVICE_KEYBOARD) {
      return;
    }
    wlr_keyboard* handle = wlr_keyboard_from_input_device(device);
    auto* keyboard = new Keyboard{
        .manager = this,
        .handle = handle,
    };
    keyboard->key.notify = OnKeyboardState;
    wl_signal_add(&handle->events.key, &keyboard->key);
    keyboard->modifiers.notify = OnKeyboardModifiers;
    wl_signal_add(&handle->events.modifiers, &keyboard->modifiers);
    keyboard->destroy.notify = OnKeyboardDestroy;
    wl_signal_add(&device->events.destroy, &keyboard->destroy);
    keyboards.push_back(keyboard);
  }

  void NotifyActivity() { kde_idle->NotifyActivity(); }

  void MapToplevel(wlr_surface* surface) {
    if (surface == nullptr || FindWindow(surface) != nullptr) {
      return;
    }
    auto window = std::make_unique<PlasmaWindow>();
    window->manager = this;
    window->surface = surface;
    window->id = next_window_id++;
    window->uuid = "flakewm-window-" + std::to_string(window->id);
    PlasmaWindow* added = window.get();
    windows.push_back(std::move(window));
    for (wl_resource* resource : window_management_resources) {
      AnnounceWindow(resource, added);
    }
    ApplyServerDecoration(surface);
    if (Blur* blur = FindBlur(surface); blur != nullptr) {
      ApplyBlur(blur);
    }
  }

  void UnmapToplevel(wlr_surface* surface) {
    PlasmaWindow* window = FindWindow(surface);
    if (window == nullptr) {
      return;
    }
    while (!window->resources.empty()) {
      wl_resource* resource = window->resources.back();
      org_kde_plasma_window_send_unmapped(resource);
      wl_resource_set_user_data(resource, nullptr);
      wl_resource_set_destructor(resource, nullptr);
      window->resources.pop_back();
    }
    std::erase(show_desktop_surfaces, surface);
    std::erase_if(windows, [window](const std::unique_ptr<PlasmaWindow>& item) {
      return item.get() == window;
    });
  }

  void UpdateToplevel(wlr_surface* surface) {
    PlasmaWindow* window = FindWindow(surface);
    if (window == nullptr) {
      return;
    }
    for (wl_resource* resource : window->resources) {
      SendWindow(resource, window);
    }
  }

  void UpdateWorkspaces() {
    const int count = compositor->workspace_count_;
    for (wl_resource* management : desktop_management_resources) {
      if (announced_workspace_count < count) {
        for (int index = announced_workspace_count; index < count; ++index) {
          const std::string id = DesktopId(index);
          org_kde_plasma_virtual_desktop_management_send_desktop_created(
              management, id.c_str(), static_cast<uint32_t>(index));
        }
      } else if (announced_workspace_count > count) {
        for (int index = announced_workspace_count - 1; index >= count;
             --index) {
          const std::string id = DesktopId(index);
          org_kde_plasma_virtual_desktop_management_send_desktop_removed(
              management, id.c_str());
        }
      }
      org_kde_plasma_virtual_desktop_management_send_done(management);
    }
    announced_workspace_count = count;
    for (VirtualDesktop* desktop : desktop_resources) {
      if (desktop->index >= count) {
        org_kde_plasma_virtual_desktop_send_removed(desktop->resource);
        continue;
      }
      if (desktop->index == compositor->current_workspace_) {
        org_kde_plasma_virtual_desktop_send_activated(desktop->resource);
      } else {
        org_kde_plasma_virtual_desktop_send_deactivated(desktop->resource);
      }
      org_kde_plasma_virtual_desktop_send_done(desktop->resource);
    }
  }

  void UpdateToplevelParent(wlr_surface* surface, wlr_surface* parent) {
    PlasmaWindow* window = FindWindow(surface);
    if (window == nullptr) {
      return;
    }
    window->parent = parent;
    for (wl_resource* resource : window->resources) {
      if (wl_resource_get_version(resource) <
          ORG_KDE_PLASMA_WINDOW_PARENT_WINDOW_SINCE_VERSION) {
        continue;
      }
      wl_resource* parent_resource = nullptr;
      if (PlasmaWindow* parent_window = FindWindow(parent);
          parent_window != nullptr) {
        wl_client* client = wl_resource_get_client(resource);
        auto found = std::find_if(
            parent_window->resources.begin(), parent_window->resources.end(),
            [client](wl_resource* candidate) {
              return wl_resource_get_client(candidate) == client;
            });
        if (found != parent_window->resources.end()) {
          parent_resource = *found;
        }
      }
      org_kde_plasma_window_send_parent_window(resource, parent_resource);
    }
  }

  struct Keyboard {
    Impl* manager;
    wlr_keyboard* handle;
    wl_listener key = {};
    wl_listener modifiers = {};
    wl_listener destroy = {};
  };

  struct PlasmaSurface {
    Impl* manager;
    wl_resource* resource;
    wlr_surface* surface;
    wlr_output* output = nullptr;
    int32_t x = 0;
    int32_t y = 0;
    bool position_set = false;
    uint32_t role = ORG_KDE_PLASMA_SURFACE_ROLE_NORMAL;
    uint32_t panel_behavior =
        ORG_KDE_PLASMA_SURFACE_PANEL_BEHAVIOR_ALWAYS_VISIBLE;
    bool role_set = false;
    bool skip_taskbar = false;
    bool skip_switcher = false;
    bool panel_takes_focus = false;
    bool hidden = false;
    wl_listener destroy = {};
  };

  struct PlasmaWindow {
    Impl* manager;
    wlr_surface* surface;
    wlr_surface* parent = nullptr;
    uint32_t id;
    std::string uuid;
    std::vector<wl_resource*> resources;
  };

  struct Blur {
    Impl* manager;
    wl_resource* resource;
    wlr_surface* surface;
    pixman_region32_t current_region;
    pixman_region32_t pending_region;
    uint32_t strength = UINT32_MAX;
    uint32_t pending_strength = UINT32_MAX;
    wl_listener destroy = {};
  };

  struct ServerDecoration {
    Impl* manager;
    wlr_server_decoration* handle;
    bool active = true;
    bool server_side = false;
    wl_listener mode = {};
    wl_listener destroy = {};
  };

  struct NewServerDecorationListener {
    Impl* manager = nullptr;
    wl_listener listener = {};
  };

  static void OnNewServerDecoration(wl_listener* listener, void* data) {
    NewServerDecorationListener* state =
        wl_container_of(listener, state, listener);
    Impl* manager = state->manager;
    auto* handle = static_cast<wlr_server_decoration*>(data);
    ServerDecoration* previous =
        manager->ActiveServerDecoration(handle->surface);
    const bool has_xdg = manager->compositor->HasXdgDecoration(handle->surface);
    auto* decoration = new ServerDecoration{
        .manager = manager,
        .handle = handle,
        .server_side =
            previous != nullptr
                ? previous->server_side
                : (has_xdg ? manager->compositor->SsdEnabledForSurface(
                                 handle->surface)
                           : handle->mode ==
                                 WLR_SERVER_DECORATION_MANAGER_MODE_SERVER),
    };
    if (previous != nullptr) {
      previous->active = false;
    }
    decoration->mode.notify = OnServerDecorationMode;
    wl_signal_add(&handle->events.mode, &decoration->mode);
    decoration->destroy.notify = OnServerDecorationDestroy;
    wl_signal_add(&handle->events.destroy, &decoration->destroy);
    manager->server_decorations.push_back(decoration);
    if (previous == nullptr && !has_xdg) {
      manager->ApplyServerDecoration(handle->surface);
    }
  }

  static void OnServerDecorationMode(wl_listener* listener, void*) {
    ServerDecoration* decoration = wl_container_of(listener, decoration, mode);
    if (!decoration->active || decoration->handle == nullptr) {
      return;
    }
    decoration->server_side =
        decoration->handle->mode == WLR_SERVER_DECORATION_MANAGER_MODE_SERVER;
    decoration->manager->ApplyServerDecoration(decoration->handle->surface);
  }

  static void OnServerDecorationDestroy(wl_listener* listener, void*) {
    ServerDecoration* decoration =
        wl_container_of(listener, decoration, destroy);
    Impl* manager = decoration->manager;
    wl_list_remove(&decoration->mode.link);
    wl_list_remove(&decoration->destroy.link);
    std::erase(manager->server_decorations, decoration);
    delete decoration;
  }

  ServerDecoration* ActiveServerDecoration(wlr_surface* surface) const {
    if (surface == nullptr) {
      return nullptr;
    }
    const wlr_surface* root = wlr_surface_get_root_surface(surface);
    auto found = std::find_if(
        server_decorations.rbegin(), server_decorations.rend(),
        [root](const ServerDecoration* decoration) {
          return decoration != nullptr && decoration->active &&
                 decoration->handle != nullptr &&
                 decoration->handle->surface != nullptr &&
                 wlr_surface_get_root_surface(decoration->handle->surface) ==
                     root;
        });
    return found == server_decorations.rend() ? nullptr : *found;
  }

  void ApplyServerDecoration(wlr_surface* surface) const {
    ServerDecoration* decoration = ActiveServerDecoration(surface);
    if (decoration == nullptr) {
      return;
    }
    core::CompositorPrivate::Toplevel* toplevel =
        compositor->ToplevelForSurface(surface);
    if (toplevel == nullptr) {
      return;
    }
    compositor->SetSsdEnabled(toplevel, decoration->server_side);
  }

  static void BindKeyState(wl_client* client, void* data, uint32_t version,
                           uint32_t id) {
    static const struct org_kde_kwin_keystate_interface implementation = {
        .fetchStates = FetchKeyStates,
        .destroy = DestroyKeyState,
    };
    auto* manager = static_cast<Impl*>(data);
    wl_resource* resource =
        wl_resource_create(client, &org_kde_kwin_keystate_interface,
                           std::min(version, kKeyStateVersion), id);
    if (resource == nullptr) {
      wl_client_post_no_memory(client);
      return;
    }
    manager->keystate_resources.push_back(resource);
    wl_resource_set_implementation(resource, &implementation, manager,
                                   RemoveKeyState);
  }

  static void FetchKeyStates(wl_client*, wl_resource* resource) {
    auto* manager = static_cast<Impl*>(wl_resource_get_user_data(resource));
    manager->SendKeyStates(resource);
  }

  static void DestroyKeyState(wl_client*, wl_resource* resource) {
    wl_resource_destroy(resource);
  }

  static void RemoveKeyState(wl_resource* resource) {
    auto* manager = static_cast<Impl*>(wl_resource_get_user_data(resource));
    if (manager != nullptr) {
      std::erase(manager->keystate_resources, resource);
    }
  }

  static void OnKeyboardState(wl_listener* listener, void*) {
    Keyboard* keyboard = wl_container_of(listener, keyboard, key);
    for (wl_resource* resource : keyboard->manager->keystate_resources) {
      keyboard->manager->SendKeyStates(resource);
    }
  }

  static void OnKeyboardModifiers(wl_listener* listener, void*) {
    Keyboard* keyboard = wl_container_of(listener, keyboard, modifiers);
    for (wl_resource* resource : keyboard->manager->keystate_resources) {
      keyboard->manager->SendKeyStates(resource);
    }
  }

  static void OnKeyboardDestroy(wl_listener* listener, void*) {
    Keyboard* keyboard = wl_container_of(listener, keyboard, destroy);
    Impl* manager = keyboard->manager;
    wl_list_remove(&keyboard->key.link);
    wl_list_remove(&keyboard->modifiers.link);
    wl_list_remove(&keyboard->destroy.link);
    std::erase(manager->keyboards, keyboard);
    delete keyboard;
  }

  void SendKeyStates(wl_resource* resource) const {
    wlr_keyboard* keyboard = seat->keyboard_state.keyboard;
    const std::array<std::pair<uint32_t, const char*>, 3> keys = {{
        {ORG_KDE_KWIN_KEYSTATE_KEY_CAPSLOCK, XKB_LED_NAME_CAPS},
        {ORG_KDE_KWIN_KEYSTATE_KEY_NUMLOCK, XKB_LED_NAME_NUM},
        {ORG_KDE_KWIN_KEYSTATE_KEY_SCROLLLOCK, XKB_LED_NAME_SCROLL},
    }};
    for (const auto& [key, led] : keys) {
      const bool locked =
          keyboard != nullptr && keyboard->xkb_state != nullptr &&
          xkb_state_led_name_is_active(keyboard->xkb_state, led) > 0;
      org_kde_kwin_keystate_send_stateChanged(
          resource, key,
          locked ? ORG_KDE_KWIN_KEYSTATE_STATE_LOCKED
                 : ORG_KDE_KWIN_KEYSTATE_STATE_UNLOCKED);
    }
  }

  static void BindDesktopManagement(wl_client* client, void* data,
                                    uint32_t version, uint32_t id) {
    static const struct org_kde_plasma_virtual_desktop_management_interface
        implementation = {
            .get_virtual_desktop = GetVirtualDesktop,
            .request_create_virtual_desktop = RequestCreateVirtualDesktop,
            .request_remove_virtual_desktop = RequestRemoveVirtualDesktop,
        };
    auto* manager = static_cast<Impl*>(data);
    wl_resource* resource = wl_resource_create(
        client, &org_kde_plasma_virtual_desktop_management_interface,
        std::min(version, kVirtualDesktopVersion), id);
    if (resource == nullptr) {
      wl_client_post_no_memory(client);
      return;
    }
    manager->desktop_management_resources.push_back(resource);
    wl_resource_set_implementation(resource, &implementation, manager,
                                   RemoveDesktopManagement);
    for (int index = 0; index < manager->compositor->workspace_count_;
         ++index) {
      const std::string desktop_id = DesktopId(index);
      org_kde_plasma_virtual_desktop_management_send_desktop_created(
          resource, desktop_id.c_str(), static_cast<uint32_t>(index));
    }
    if (wl_resource_get_version(resource) >=
        ORG_KDE_PLASMA_VIRTUAL_DESKTOP_MANAGEMENT_ROWS_SINCE_VERSION) {
      org_kde_plasma_virtual_desktop_management_send_rows(resource, 1);
    }
    org_kde_plasma_virtual_desktop_management_send_done(resource);
  }

  static void RemoveDesktopManagement(wl_resource* resource) {
    auto* manager = static_cast<Impl*>(wl_resource_get_user_data(resource));
    if (manager != nullptr) {
      std::erase(manager->desktop_management_resources, resource);
    }
  }

  static void GetVirtualDesktop(wl_client* client, wl_resource* resource,
                                uint32_t id, const char* desktop_id) {
    static const struct org_kde_plasma_virtual_desktop_interface
        implementation = {
            .request_activate = ActivateVirtualDesktop,
        };
    auto* manager = static_cast<Impl*>(wl_resource_get_user_data(resource));
    wl_resource* desktop = wl_resource_create(
        client, &org_kde_plasma_virtual_desktop_interface, 1, id);
    if (desktop == nullptr) {
      wl_client_post_no_memory(client);
      return;
    }
    const std::optional<int> index = DesktopIndex(Safe(desktop_id));
    if (!index.has_value() || *index >= manager->compositor->workspace_count_) {
      wl_resource_set_implementation(desktop, &implementation, nullptr,
                                     nullptr);
      return;
    }
    auto* state = new VirtualDesktop{
        .manager = manager, .resource = desktop, .index = *index};
    manager->desktop_resources.push_back(state);
    wl_resource_set_implementation(desktop, &implementation, state,
                                   RemoveDesktop);
    const std::string id_value = DesktopId(*index);
    const std::string name = DesktopName(*index);
    org_kde_plasma_virtual_desktop_send_desktop_id(desktop, id_value.c_str());
    org_kde_plasma_virtual_desktop_send_name(desktop, name.c_str());
    if (*index == manager->compositor->current_workspace_) {
      org_kde_plasma_virtual_desktop_send_activated(desktop);
    } else {
      org_kde_plasma_virtual_desktop_send_deactivated(desktop);
    }
    org_kde_plasma_virtual_desktop_send_done(desktop);
  }

  static void ActivateVirtualDesktop(wl_client*, wl_resource* resource) {
    auto* desktop =
        static_cast<VirtualDesktop*>(wl_resource_get_user_data(resource));
    if (desktop == nullptr) return;
    desktop->manager->compositor->SwitchWorkspace(desktop->index);
  }

  static void RemoveDesktop(wl_resource* resource) {
    auto* desktop =
        static_cast<VirtualDesktop*>(wl_resource_get_user_data(resource));
    if (desktop == nullptr) return;
    std::erase(desktop->manager->desktop_resources, desktop);
    delete desktop;
  }

  static void RequestCreateVirtualDesktop(wl_client*, wl_resource* resource,
                                          const char*, uint32_t) {
    auto* manager = static_cast<Impl*>(wl_resource_get_user_data(resource));
    manager->compositor->AddWorkspace();
  }

  static void RequestRemoveVirtualDesktop(wl_client*, wl_resource* resource,
                                          const char* id) {
    auto* manager = static_cast<Impl*>(wl_resource_get_user_data(resource));
    const std::optional<int> index = DesktopIndex(Safe(id));
    if (index.has_value() && *index < manager->compositor->workspace_count_) {
      manager->compositor->RemoveWorkspace(*index);
    }
  }

  static void BindShell(wl_client* client, void* data, uint32_t version,
                        uint32_t id) {
    static const struct org_kde_plasma_shell_interface implementation = {
        .get_surface = GetPlasmaSurface,
    };
    wl_resource* resource =
        wl_resource_create(client, &org_kde_plasma_shell_interface,
                           std::min(version, kPlasmaShellVersion), id);
    if (resource == nullptr) {
      wl_client_post_no_memory(client);
      return;
    }
    wl_resource_set_implementation(resource, &implementation, data, nullptr);
  }

  static void GetPlasmaSurface(wl_client* client, wl_resource* resource,
                               uint32_t id, wl_resource* surface_resource) {
    static const struct org_kde_plasma_surface_interface implementation = {
        .destroy = DestroyPlasmaSurfaceRequest,
        .set_output = SetPlasmaOutput,
        .set_position = SetPlasmaPosition,
        .set_role = SetPlasmaRole,
        .set_panel_behavior = SetPanelBehavior,
        .set_skip_taskbar = SetSkipTaskbar,
        .panel_auto_hide_hide = HidePanel,
        .panel_auto_hide_show = ShowPanel,
        .set_panel_takes_focus = SetPanelTakesFocus,
        .set_skip_switcher = SetSkipSwitcher,
    };
    auto* manager = static_cast<Impl*>(wl_resource_get_user_data(resource));
    wlr_surface* surface = wlr_surface_from_resource(surface_resource);
    if (surface == nullptr) {
      return;
    }
    if (manager->FindPlasmaSurface(surface) != nullptr) {
      wl_client_post_implementation_error(
          client, "wl_surface already has a Plasma shell surface");
      return;
    }
    wl_resource* plasma_resource =
        wl_resource_create(client, &org_kde_plasma_surface_interface,
                           wl_resource_get_version(resource), id);
    if (plasma_resource == nullptr) {
      wl_client_post_no_memory(client);
      return;
    }
    auto* plasma = new PlasmaSurface{
        .manager = manager,
        .resource = plasma_resource,
        .surface = surface,
    };
    plasma->destroy.notify = OnPlasmaWlSurfaceDestroy;
    wl_signal_add(&surface->events.destroy, &plasma->destroy);
    manager->plasma_surfaces.push_back(plasma);
    wl_resource_set_implementation(plasma_resource, &implementation, plasma,
                                   DestroyPlasmaSurface);
  }

  static void DestroyPlasmaSurfaceRequest(wl_client*, wl_resource* resource) {
    wl_resource_destroy(resource);
  }

  static void DestroyPlasmaSurface(wl_resource* resource) {
    auto* surface =
        static_cast<PlasmaSurface*>(wl_resource_get_user_data(resource));
    if (surface == nullptr) {
      return;
    }
    if (core::CompositorPrivate::Toplevel* toplevel =
            surface->manager->compositor->ToplevelForSurface(surface->surface);
        toplevel != nullptr && toplevel->scene_tree != nullptr &&
        toplevel->scene_tree->node.parent !=
            surface->manager->compositor->toplevel_tree_) {
      wlr_scene_node_reparent(&toplevel->scene_tree->node,
                              surface->manager->compositor->toplevel_tree_);
    }
    if (surface->surface != nullptr) {
      wl_list_remove(&surface->destroy.link);
    }
    std::erase(surface->manager->plasma_surfaces, surface);
    delete surface;
  }

  static void OnPlasmaWlSurfaceDestroy(wl_listener* listener, void*) {
    PlasmaSurface* surface = wl_container_of(listener, surface, destroy);
    surface->surface = nullptr;
    wl_list_remove(&surface->destroy.link);
  }

  static void SetPlasmaOutput(wl_client*, wl_resource* resource,
                              wl_resource* output) {
    auto* surface =
        static_cast<PlasmaSurface*>(wl_resource_get_user_data(resource));
    surface->output =
        output == nullptr ? nullptr : wlr_output_from_resource(output);
  }

  static void SetPlasmaPosition(wl_client*, wl_resource* resource, int32_t x,
                                int32_t y) {
    auto* surface =
        static_cast<PlasmaSurface*>(wl_resource_get_user_data(resource));
    surface->x =
        std::clamp(x, -kMaximumSceneCoordinate, kMaximumSceneCoordinate);
    surface->y =
        std::clamp(y, -kMaximumSceneCoordinate, kMaximumSceneCoordinate);
    surface->position_set = true;
    surface->manager->ApplyPlasmaSurface(surface);
  }

  static void SetPlasmaRole(wl_client*, wl_resource* resource, uint32_t role) {
    auto* surface =
        static_cast<PlasmaSurface*>(wl_resource_get_user_data(resource));
    if (surface->role_set || !org_kde_plasma_surface_role_is_valid(
                                 role, wl_resource_get_version(resource))) {
      return;
    }
    surface->role_set = true;
    surface->role = role;
    surface->manager->ApplyPlasmaSurface(surface);
  }

  static void SetPanelBehavior(wl_client*, wl_resource* resource,
                               uint32_t behavior) {
    auto* surface =
        static_cast<PlasmaSurface*>(wl_resource_get_user_data(resource));
    if (org_kde_plasma_surface_panel_behavior_is_valid(
            behavior, wl_resource_get_version(resource))) {
      surface->panel_behavior = behavior;
    }
  }

  static void SetSkipTaskbar(wl_client*, wl_resource* resource, uint32_t skip) {
    auto* surface =
        static_cast<PlasmaSurface*>(wl_resource_get_user_data(resource));
    surface->skip_taskbar = skip != 0;
    surface->manager->UpdateToplevel(surface->surface);
  }

  static bool ValidateAutoHide(PlasmaSurface* surface) {
    if (surface->role == ORG_KDE_PLASMA_SURFACE_ROLE_PANEL &&
        surface->panel_behavior ==
            ORG_KDE_PLASMA_SURFACE_PANEL_BEHAVIOR_AUTO_HIDE) {
      return true;
    }
    wl_resource_post_error(surface->resource,
                           ORG_KDE_PLASMA_SURFACE_ERROR_PANEL_NOT_AUTO_HIDE,
                           "surface is not an auto-hide panel");
    return false;
  }

  static void HidePanel(wl_client*, wl_resource* resource) {
    auto* surface =
        static_cast<PlasmaSurface*>(wl_resource_get_user_data(resource));
    if (!ValidateAutoHide(surface)) {
      return;
    }
    surface->hidden = true;
    surface->manager->ApplyPlasmaSurface(surface);
    org_kde_plasma_surface_send_auto_hidden_panel_hidden(resource);
  }

  static void ShowPanel(wl_client*, wl_resource* resource) {
    auto* surface =
        static_cast<PlasmaSurface*>(wl_resource_get_user_data(resource));
    if (!ValidateAutoHide(surface)) {
      return;
    }
    surface->hidden = false;
    surface->manager->ApplyPlasmaSurface(surface);
    org_kde_plasma_surface_send_auto_hidden_panel_shown(resource);
  }

  static void SetPanelTakesFocus(wl_client*, wl_resource* resource,
                                 uint32_t takes_focus) {
    auto* surface =
        static_cast<PlasmaSurface*>(wl_resource_get_user_data(resource));
    surface->panel_takes_focus = takes_focus != 0;
  }

  static void SetSkipSwitcher(wl_client*, wl_resource* resource,
                              uint32_t skip) {
    auto* surface =
        static_cast<PlasmaSurface*>(wl_resource_get_user_data(resource));
    surface->skip_switcher = skip != 0;
    surface->manager->UpdateToplevel(surface->surface);
  }

  PlasmaSurface* FindPlasmaSurface(wlr_surface* surface) const {
    if (surface == nullptr) {
      return nullptr;
    }
    const wlr_surface* root = wlr_surface_get_root_surface(surface);
    auto it = std::find_if(
        plasma_surfaces.begin(), plasma_surfaces.end(),
        [root](PlasmaSurface* item) {
          return item->surface != nullptr &&
                 wlr_surface_get_root_surface(item->surface) == root;
        });
    return it == plasma_surfaces.end() ? nullptr : *it;
  }

  void ApplyPlasmaSurface(PlasmaSurface* surface) const {
    if (surface == nullptr || surface->surface == nullptr) {
      return;
    }
    core::CompositorPrivate::Toplevel* toplevel =
        compositor->ToplevelForSurface(surface->surface);
    if (toplevel == nullptr || toplevel->scene_tree == nullptr) {
      return;
    }
    wlr_scene_tree* parent = compositor->toplevel_tree_;
    switch (surface->role) {
      case ORG_KDE_PLASMA_SURFACE_ROLE_DESKTOP:
        parent = compositor
                     ->shell_layer_trees_[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND];
        break;
      case ORG_KDE_PLASMA_SURFACE_ROLE_PANEL:
        parent = compositor->shell_layer_trees_[ZWLR_LAYER_SHELL_V1_LAYER_TOP];
        break;
      case ORG_KDE_PLASMA_SURFACE_ROLE_ONSCREENDISPLAY:
      case ORG_KDE_PLASMA_SURFACE_ROLE_NOTIFICATION:
      case ORG_KDE_PLASMA_SURFACE_ROLE_CRITICALNOTIFICATION:
      case ORG_KDE_PLASMA_SURFACE_ROLE_TOOLTIP:
        parent =
            compositor->shell_layer_trees_[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY];
        break;
      default:
        break;
    }
    if (parent != nullptr && toplevel->scene_tree->node.parent != parent) {
      wlr_scene_node_reparent(&toplevel->scene_tree->node, parent);
    }
    // Other requests reapply the surface too; only move on set_position.
    if (surface->position_set) {
      toplevel->position_requested = true;
      wlr_scene_node_set_position(&toplevel->scene_tree->node, surface->x,
                                  surface->y);
    }
    wlr_scene_node_set_enabled(&toplevel->scene_tree->node, !surface->hidden);
  }

  static void BindWindowManagement(wl_client* client, void* data,
                                   uint32_t version, uint32_t id) {
    static const struct org_kde_plasma_window_management_interface
        implementation = {
            .show_desktop = ShowDesktop,
            .get_window = GetWindow,
            .get_window_by_uuid = GetWindowByUuid,
        };
    auto* manager = static_cast<Impl*>(data);
    wl_resource* resource =
        wl_resource_create(client, &org_kde_plasma_window_management_interface,
                           std::min(version, kPlasmaWindowVersion), id);
    if (resource == nullptr) {
      wl_client_post_no_memory(client);
      return;
    }
    manager->window_management_resources.push_back(resource);
    wl_resource_set_implementation(resource, &implementation, manager,
                                   RemoveWindowManagement);
    org_kde_plasma_window_management_send_show_desktop_changed(
        resource, manager->showing_desktop
                      ? ORG_KDE_PLASMA_WINDOW_MANAGEMENT_SHOW_DESKTOP_ENABLED
                      : ORG_KDE_PLASMA_WINDOW_MANAGEMENT_SHOW_DESKTOP_DISABLED);
    for (const std::unique_ptr<PlasmaWindow>& window : manager->windows) {
      manager->AnnounceWindow(resource, window.get());
    }
  }

  static void RemoveWindowManagement(wl_resource* resource) {
    auto* manager = static_cast<Impl*>(wl_resource_get_user_data(resource));
    if (manager != nullptr) {
      std::erase(manager->window_management_resources, resource);
    }
  }

  static void ShowDesktop(wl_client*, wl_resource* resource, uint32_t state) {
    auto* manager = static_cast<Impl*>(wl_resource_get_user_data(resource));
    const bool show =
        state == ORG_KDE_PLASMA_WINDOW_MANAGEMENT_SHOW_DESKTOP_ENABLED;
    if (manager->showing_desktop == show) {
      return;
    }
    manager->showing_desktop = show;
    if (show) {
      manager->show_desktop_surfaces.clear();
      for (const std::unique_ptr<PlasmaWindow>& window : manager->windows) {
        core::CompositorPrivate::Toplevel* toplevel =
            manager->compositor->ToplevelForSurface(window->surface);
        if (toplevel != nullptr && !toplevel->minimized &&
            toplevel->workspace == manager->compositor->current_workspace_) {
          manager->show_desktop_surfaces.push_back(window->surface);
          manager->compositor->protocol_manager_->RequestMinimize(
              window->surface, true);
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
      org_kde_plasma_window_management_send_show_desktop_changed(management,
                                                                 state);
    }
  }

  static void GetWindow(wl_client* client, wl_resource* resource, uint32_t id,
                        uint32_t window_id) {
    auto* manager = static_cast<Impl*>(wl_resource_get_user_data(resource));
    auto it = std::find_if(
        manager->windows.begin(), manager->windows.end(),
        [window_id](const auto& item) { return item->id == window_id; });
    if (it != manager->windows.end()) {
      manager->AddWindowResource(client, resource, id, it->get());
    }
  }

  static void GetWindowByUuid(wl_client* client, wl_resource* resource,
                              uint32_t id, const char* uuid) {
    auto* manager = static_cast<Impl*>(wl_resource_get_user_data(resource));
    auto it = std::find_if(
        manager->windows.begin(), manager->windows.end(),
        [uuid](const auto& item) { return item->uuid == Safe(uuid); });
    if (it != manager->windows.end()) {
      manager->AddWindowResource(client, resource, id, it->get());
    }
  }

  static void SetWindowState(wl_client*, wl_resource* resource, uint32_t flags,
                             uint32_t state) {
    auto* window =
        static_cast<PlasmaWindow*>(wl_resource_get_user_data(resource));
    if (window == nullptr || window->surface == nullptr) {
      return;
    }
    ProtocolManager* protocols =
        window->manager->compositor->protocol_manager_.get();
    if ((flags & ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_ACTIVE) != 0 &&
        (state & ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_ACTIVE) != 0) {
      protocols->RequestActivate(window->surface, window->manager->seat);
    }
    if ((flags & ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_MINIMIZED) != 0) {
      protocols->RequestMinimize(
          window->surface,
          (state & ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_MINIMIZED) != 0);
    }
    if ((flags & ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_MAXIMIZED) != 0) {
      protocols->RequestMaximize(
          window->surface,
          (state & ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_MAXIMIZED) != 0);
    }
    if ((flags & ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_FULLSCREEN) != 0) {
      protocols->RequestFullscreen(
          window->surface,
          (state & ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_FULLSCREEN) != 0,
          nullptr);
    }
    if ((flags & ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_KEEP_ABOVE) != 0) {
      core::CompositorPrivate::Toplevel* toplevel =
          window->manager->compositor->ToplevelForSurface(window->surface);
      window->manager->compositor->SetKeptAbove(
          toplevel,
          (state & ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_KEEP_ABOVE) != 0);
    }
    window->manager->UpdateToplevel(window->surface);
  }

  static void SetVirtualDesktop(wl_client*, wl_resource* resource,
                                uint32_t number) {
    auto* window =
        static_cast<PlasmaWindow*>(wl_resource_get_user_data(resource));
    if (window == nullptr ||
        number >= static_cast<uint32_t>(
                      window->manager->compositor->workspace_count_))
      return;
    core::CompositorPrivate::Toplevel* toplevel =
        window->manager->compositor->ToplevelForSurface(window->surface);
    window->manager->compositor->SetAllWorkspaces(toplevel, false);
    window->manager->compositor->MoveToplevelToWorkspace(
        toplevel, static_cast<int>(number));
  }
  static void NoopMinimizedGeometry(wl_client*, wl_resource*, wl_resource*,
                                    uint32_t, uint32_t, uint32_t, uint32_t) {}
  static void NoopUnsetMinimizedGeometry(wl_client*, wl_resource*,
                                         wl_resource*) {}

  static void CloseWindow(wl_client*, wl_resource* resource) {
    auto* window =
        static_cast<PlasmaWindow*>(wl_resource_get_user_data(resource));
    if (window != nullptr) {
      window->manager->compositor->protocol_manager_->RequestClose(
          window->surface);
    }
  }

  static void NoopWindowRequest(wl_client*, wl_resource*) {}
  static void DestroyWindowRequest(wl_client*, wl_resource* resource) {
    wl_resource_destroy(resource);
  }
  static void GetWindowIcon(wl_client*, wl_resource*, int32_t fd) { close(fd); }
  static void EnterVirtualDesktop(wl_client*, wl_resource* resource,
                                  const char* desktop_id) {
    auto* window =
        static_cast<PlasmaWindow*>(wl_resource_get_user_data(resource));
    const std::optional<int> index = DesktopIndex(Safe(desktop_id));
    if (window == nullptr || !index.has_value() ||
        *index >= window->manager->compositor->workspace_count_)
      return;
    core::CompositorPrivate::Toplevel* toplevel =
        window->manager->compositor->ToplevelForSurface(window->surface);
    window->manager->compositor->SetAllWorkspaces(toplevel, false);
    window->manager->compositor->MoveToplevelToWorkspace(toplevel, *index);
  }
  static void NoopDesktopId(wl_client*, wl_resource*, const char*) {}
  static void NoopSendToOutput(wl_client*, wl_resource*, wl_resource*) {}

  static void DestroyWindowResource(wl_resource* resource) {
    auto* window =
        static_cast<PlasmaWindow*>(wl_resource_get_user_data(resource));
    if (window != nullptr) {
      std::erase(window->resources, resource);
    }
  }

  void AddWindowResource(wl_client* client, wl_resource* management,
                         uint32_t id, PlasmaWindow* window) {
    static const struct org_kde_plasma_window_interface implementation = {
        .set_state = SetWindowState,
        .set_virtual_desktop = SetVirtualDesktop,
        .set_minimized_geometry = NoopMinimizedGeometry,
        .unset_minimized_geometry = NoopUnsetMinimizedGeometry,
        .close = CloseWindow,
        .request_move = NoopWindowRequest,
        .request_resize = NoopWindowRequest,
        .destroy = DestroyWindowRequest,
        .get_icon = GetWindowIcon,
        .request_enter_virtual_desktop = EnterVirtualDesktop,
        .request_enter_new_virtual_desktop = NoopWindowRequest,
        .request_leave_virtual_desktop = NoopDesktopId,
        .request_enter_activity = NoopDesktopId,
        .request_leave_activity = NoopDesktopId,
        .send_to_output = NoopSendToOutput,
    };
    wl_resource* resource =
        wl_resource_create(client, &org_kde_plasma_window_interface,
                           wl_resource_get_version(management), id);
    if (resource == nullptr) {
      wl_client_post_no_memory(client);
      return;
    }
    window->resources.push_back(resource);
    wl_resource_set_implementation(resource, &implementation, window,
                                   DestroyWindowResource);
    SendWindow(resource, window);
    if (wl_resource_get_version(resource) >=
        ORG_KDE_PLASMA_WINDOW_INITIAL_STATE_SINCE_VERSION) {
      org_kde_plasma_window_send_initial_state(resource);
    }
  }

  void AnnounceWindow(wl_resource* management, PlasmaWindow* window) const {
    if (wl_resource_get_version(management) >=
        ORG_KDE_PLASMA_WINDOW_MANAGEMENT_WINDOW_WITH_UUID_SINCE_VERSION) {
      org_kde_plasma_window_management_send_window_with_uuid(
          management, window->id, window->uuid.c_str());
    } else {
      org_kde_plasma_window_management_send_window(management, window->id);
    }
  }

  uint32_t WindowState(PlasmaWindow* window) const {
    core::CompositorPrivate::Toplevel* toplevel =
        compositor->ToplevelForSurface(window->surface);
    if (toplevel == nullptr) {
      return 0;
    }
    uint32_t state = ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_CLOSEABLE |
                     ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_MOVABLE |
                     ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_RESIZABLE;
    if (seat->keyboard_state.focused_surface == window->surface) {
      state |= ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_ACTIVE;
    }
    if (toplevel->minimized) {
      state |= ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_MINIMIZED;
    }
    if (toplevel->maximized) {
      state |= ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_MAXIMIZED;
    }
    if (toplevel->kept_above) {
      state |= ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_KEEP_ABOVE;
    }
    if (toplevel->RequestedFullscreen()) {
      state |= ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_FULLSCREEN;
    }
    if (toplevel->CanMinimize()) {
      state |= ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_MINIMIZABLE;
    }
    if (toplevel->CanMaximize()) {
      state |= ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_MAXIMIZABLE;
    }
    state |= ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_FULLSCREENABLE;
    if (PlasmaSurface* plasma = FindPlasmaSurface(window->surface);
        plasma != nullptr) {
      if (plasma->skip_taskbar) {
        state |= ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_SKIPTASKBAR;
      }
      if (plasma->skip_switcher) {
        state |= ORG_KDE_PLASMA_WINDOW_MANAGEMENT_STATE_SKIPSWITCHER;
      }
    }
    return state;
  }

  void SendWindow(wl_resource* resource, PlasmaWindow* window) const {
    core::CompositorPrivate::Toplevel* toplevel =
        compositor->ToplevelForSurface(window->surface);
    if (toplevel == nullptr) {
      return;
    }
    org_kde_plasma_window_send_title_changed(resource, Safe(toplevel->Title()));
    org_kde_plasma_window_send_app_id_changed(resource,
                                              Safe(toplevel->AppId()));
    org_kde_plasma_window_send_state_changed(resource, WindowState(window));

    pid_t pid = 0;
    wl_client_get_credentials(wl_resource_get_client(window->surface->resource),
                              &pid, nullptr, nullptr);
    if (pid > 0) {
      org_kde_plasma_window_send_pid_changed(resource,
                                             static_cast<uint32_t>(pid));
    }
    if (wl_resource_get_version(resource) >=
        ORG_KDE_PLASMA_WINDOW_GEOMETRY_SINCE_VERSION) {
      const wlr_box geometry = toplevel->FrameGeometry();
      const int x = toplevel->scene_tree == nullptr
                        ? geometry.x
                        : toplevel->scene_tree->node.x + geometry.x;
      const int y = toplevel->scene_tree == nullptr
                        ? geometry.y
                        : toplevel->scene_tree->node.y + geometry.y;
      org_kde_plasma_window_send_geometry(resource, x, y,
                                          std::max(geometry.width, 0),
                                          std::max(geometry.height, 0));
    }
    if (wl_resource_get_version(resource) >=
        ORG_KDE_PLASMA_WINDOW_VIRTUAL_DESKTOP_ENTERED_SINCE_VERSION) {
      for (int index = 0; index < compositor->workspace_count_; ++index) {
        const std::string desktop_id = DesktopId(index);
        if (toplevel->all_workspaces || index == toplevel->workspace) {
          org_kde_plasma_window_send_virtual_desktop_entered(
              resource, desktop_id.c_str());
        } else {
          org_kde_plasma_window_send_virtual_desktop_left(resource,
                                                          desktop_id.c_str());
        }
      }
    }
  }

  PlasmaWindow* FindWindow(wlr_surface* surface) const {
    if (surface == nullptr) {
      return nullptr;
    }
    const wlr_surface* root = wlr_surface_get_root_surface(surface);
    auto it = std::find_if(
        windows.begin(), windows.end(), [root](const auto& window) {
          return window->surface != nullptr &&
                 wlr_surface_get_root_surface(window->surface) == root;
        });
    return it == windows.end() ? nullptr : it->get();
  }

  Blur* FindBlur(wlr_surface* surface) const {
    auto it = std::find_if(
        blurs.rbegin(), blurs.rend(),
        [surface](const Blur* blur) { return blur->surface == surface; });
    return it == blurs.rend() ? nullptr : *it;
  }

  void ApplyBlur(Blur* blur) const {
    if (blur == nullptr || blur->surface == nullptr ||
        compositor->backdrop_blur_renderer_ == nullptr) {
      return;
    }
    if (!global_blur_enabled) {
      compositor->backdrop_blur_renderer_->ClearSurfaceBlur(blur->surface);
      compositor->UpdateBackdropBlurState();
      return;
    }
    // A client that never called set_strength gets the compositor's own level,
    // which is a rung of the shared ladder and therefore carries a pyramid
    // depth as well as an offset.  A client that did call it names its own
    // offset and gxde-wlcom blurs that at a fixed depth of 3 (kde_blur.c:61),
    // which is what kBlurIterations defaults to.
    const bool use_global_level = static_cast<int32_t>(blur->strength) == -1;
    const BlurLevel& level = BlurLevelFor(global_blur_strength);
    const float offset = use_global_level
                             ? level.offset
                             : static_cast<float>(blur->strength) / 1000.0F;
    const int iterations =
        use_global_level ? level.iterations : render::kBlurIterations;
    compositor->backdrop_blur_renderer_->SetSurfaceBlur(
        blur->surface, &blur->current_region, std::max(offset, 0.001F),
        iterations);
    compositor->UpdateBackdropBlurState();
  }

  void DisableBlur(wlr_surface* surface) const {
    if (surface == nullptr || compositor->backdrop_blur_renderer_ == nullptr) {
      return;
    }
    compositor->backdrop_blur_renderer_->ClearSurfaceBlur(surface);
    compositor->UpdateBackdropBlurState();
  }

  static void BindBlurManager(wl_client* client, void* data, uint32_t version,
                              uint32_t id) {
    static const struct org_kde_kwin_blur_manager_interface implementation = {
        .create = CreateBlur,
        .unset = UnsetBlur,
    };
    wl_resource* resource =
        wl_resource_create(client, &org_kde_kwin_blur_manager_interface,
                           std::min(version, kBlurVersion), id);
    if (resource == nullptr) {
      wl_client_post_no_memory(client);
      return;
    }
    wl_resource_set_implementation(resource, &implementation, data, nullptr);
  }

  static void CreateBlur(wl_client* client, wl_resource* resource, uint32_t id,
                         wl_resource* surface_resource) {
    static const struct org_kde_kwin_blur_interface implementation = {
        .commit = CommitBlur,
        .set_region = SetBlurRegion,
        .release = ReleaseBlur,
        .set_strength = SetBlurStrength,
    };
    auto* manager = static_cast<Impl*>(wl_resource_get_user_data(resource));
    wlr_surface* surface = wlr_surface_from_resource(surface_resource);
    wl_resource* blur_resource =
        wl_resource_create(client, &org_kde_kwin_blur_interface,
                           wl_resource_get_version(resource), id);
    if (blur_resource == nullptr) {
      wl_client_post_no_memory(client);
      return;
    }
    if (surface == nullptr) {
      wl_resource_set_implementation(blur_resource, &implementation, nullptr,
                                     nullptr);
      return;
    }
    auto* blur = new Blur{
        .manager = manager,
        .resource = blur_resource,
        .surface = surface,
    };
    pixman_region32_init(&blur->current_region);
    pixman_region32_init(&blur->pending_region);
    blur->destroy.notify = OnBlurSurfaceDestroy;
    wl_signal_add(&surface->events.destroy, &blur->destroy);
    manager->blurs.push_back(blur);
    wl_resource_set_implementation(blur_resource, &implementation, blur,
                                   DestroyBlur);
  }

  static void CommitBlur(wl_client*, wl_resource* resource) {
    auto* blur = static_cast<Blur*>(wl_resource_get_user_data(resource));
    if (blur == nullptr) {
      return;
    }
    pixman_region32_copy(&blur->current_region, &blur->pending_region);
    blur->strength = blur->pending_strength;
    blur->manager->ApplyBlur(blur);
  }

  static void SetBlurRegion(wl_client*, wl_resource* resource,
                            wl_resource* region_resource) {
    auto* blur = static_cast<Blur*>(wl_resource_get_user_data(resource));
    if (blur == nullptr) {
      return;
    }
    if (region_resource == nullptr) {
      pixman_region32_clear(&blur->pending_region);
      return;
    }
    const pixman_region32_t* region = wlr_region_from_resource(region_resource);
    if (region != nullptr) {
      pixman_region32_copy(&blur->pending_region, region);
    }
  }

  static void ReleaseBlur(wl_client*, wl_resource* resource) {
    wl_resource_destroy(resource);
  }

  static void SetBlurStrength(wl_client*, wl_resource* resource,
                              uint32_t strength) {
    auto* blur = static_cast<Blur*>(wl_resource_get_user_data(resource));
    if (blur != nullptr) {
      blur->pending_strength = strength;
    }
  }

  static void DestroyBlur(wl_resource* resource) {
    auto* blur = static_cast<Blur*>(wl_resource_get_user_data(resource));
    if (blur == nullptr) {
      return;
    }
    Impl* manager = blur->manager;
    wlr_surface* surface = blur->surface;
    if (surface != nullptr) {
      wl_list_remove(&blur->destroy.link);
    }
    pixman_region32_fini(&blur->current_region);
    pixman_region32_fini(&blur->pending_region);
    std::erase(manager->blurs, blur);
    delete blur;
    if (surface != nullptr) {
      if (Blur* replacement = manager->FindBlur(surface);
          replacement != nullptr) {
        manager->ApplyBlur(replacement);
      } else {
        manager->DisableBlur(surface);
      }
    }
  }

  static void OnBlurSurfaceDestroy(wl_listener* listener, void*) {
    Blur* blur = wl_container_of(listener, blur, destroy);
    blur->manager->DisableBlur(blur->surface);
    blur->surface = nullptr;
    wl_list_remove(&blur->destroy.link);
  }

  static void UnsetBlur(wl_client*, wl_resource* resource,
                        wl_resource* surface_resource) {
    auto* manager = static_cast<Impl*>(wl_resource_get_user_data(resource));
    wlr_surface* surface = wlr_surface_from_resource(surface_resource);
    std::vector<wl_resource*> resources;
    for (Blur* blur : manager->blurs) {
      if (blur->surface == surface) {
        resources.push_back(blur->resource);
      }
    }
    for (wl_resource* blur_resource : resources) {
      wl_resource_destroy(blur_resource);
    }
  }

  core::CompositorPrivate* compositor;
  wl_display* display = nullptr;
  wlr_seat* seat = nullptr;
  wlr_output_layout* output_layout = nullptr;
  std::unique_ptr<KdeIdleManager> kde_idle;
  std::unique_ptr<KdeOutputManager> kde_outputs;
  wl_global* keystate_global = nullptr;
  wl_global* shell_global = nullptr;
  wl_global* desktop_global = nullptr;
  wl_global* window_global = nullptr;
  wl_global* blur_global = nullptr;
  wlr_server_decoration_manager* server_decoration = nullptr;
  NewServerDecorationListener new_server_decoration;
  uint32_t next_window_id = 1;
  bool showing_desktop = false;
  std::vector<Keyboard*> keyboards;
  std::vector<wl_resource*> keystate_resources;
  std::vector<wl_resource*> desktop_management_resources;
  std::vector<VirtualDesktop*> desktop_resources;
  int announced_workspace_count = 4;
  std::vector<PlasmaSurface*> plasma_surfaces;
  std::vector<std::unique_ptr<PlasmaWindow>> windows;
  std::vector<wl_resource*> window_management_resources;
  std::vector<wlr_surface*> show_desktop_surfaces;
  std::vector<Blur*> blurs;
  bool global_blur_enabled = true;
  int global_blur_strength = 4;
  std::vector<ServerDecoration*> server_decorations;
};

KdeProtocolManager::KdeProtocolManager(core::CompositorPrivate* compositor)
    : impl_(std::make_unique<Impl>(compositor)) {}

KdeProtocolManager::~KdeProtocolManager() = default;

bool KdeProtocolManager::Create(wl_display* display, wlr_backend* backend,
                                wlr_seat* seat,
                                wlr_output_layout* output_layout) {
  return impl_->Create(display, backend, seat, output_layout);
}

void KdeProtocolManager::AddInput(wlr_input_device* device) {
  impl_->AddInput(device);
}

void KdeProtocolManager::AddOutput(wlr_output* output) {
  impl_->kde_outputs->AddOutput(output);
}

void KdeProtocolManager::UpdateOutputs() {
  impl_->kde_outputs->UpdateOutputs();
}

void KdeProtocolManager::NotifyActivity() { impl_->NotifyActivity(); }

void KdeProtocolManager::MapToplevel(wlr_surface* surface) {
  impl_->MapToplevel(surface);
}

void KdeProtocolManager::UnmapToplevel(wlr_surface* surface) {
  impl_->UnmapToplevel(surface);
}

void KdeProtocolManager::UpdateToplevel(wlr_surface* surface) {
  impl_->UpdateToplevel(surface);
}

void KdeProtocolManager::UpdateWorkspaces() { impl_->UpdateWorkspaces(); }

void KdeProtocolManager::SetGlobalBlur(bool enabled, int strength) {
  impl_->global_blur_enabled = enabled;
  impl_->global_blur_strength = std::clamp(strength, 1, 15);
  for (auto* blur : impl_->blurs) {
    impl_->ApplyBlur(blur);
  }
}

void KdeProtocolManager::UpdateToplevelParent(wlr_surface* surface,
                                              wlr_surface* parent) {
  impl_->UpdateToplevelParent(surface, parent);
}

}  // namespace protocol
}  // namespace flakewm
