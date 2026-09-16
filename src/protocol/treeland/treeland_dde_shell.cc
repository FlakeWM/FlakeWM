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

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

#include "protocol/treeland-dde-shell-v1-protocol.h"
#include "src/protocol/treeland/treeland_protocol_manager_internal.h"
#include "src/utils/signal_listener.h"

namespace flakewm {
namespace protocol {
namespace {

constexpr uint32_t kManagerVersion = 2;

class DdeShellGlobal final : public TreelandGlobal {
 public:
  struct SurfaceState {
    SurfaceState(DdeShellGlobal* new_shell, wl_resource* new_resource,
                 wlr_surface* new_surface)
        : shell(new_shell), resource(new_resource), surface(new_surface) {}

    DdeShellGlobal* shell;
    wl_resource* resource;
    wlr_surface* surface;
    int32_t x = 0;
    int32_t y = 0;
    int32_t y_offset = 0;
    bool has_position = false;
    bool auto_placement = false;
    bool overlay = false;
    bool accept_focus = true;
    utils::SignalListener<SurfaceState, void> map{this, OnMap};
    utils::SignalListener<SurfaceState, void> commit{this, OnCommit};
    utils::SignalListener<SurfaceState, void> destroy{this, OnSurfaceDestroy};

    static void OnMap(SurfaceState* state, void*) { state->Apply(); }
    static void OnCommit(SurfaceState* state, void*) { state->Apply(); }
    static void OnSurfaceDestroy(SurfaceState* state, void*) {
      state->map.Disconnect();
      state->commit.Disconnect();
      state->destroy.Disconnect();
      state->surface = nullptr;
      if (state->resource != nullptr) wl_resource_destroy(state->resource);
    }
    void Apply() const {
      if (surface == nullptr) return;
      if (overlay) shell->owner_->ApplySurfaceOverlay(surface);
      if (auto_placement) {
        shell->owner_->ApplySurfaceAutoPlacement(surface, y_offset);
      } else if (has_position) {
        shell->owner_->ApplySurfacePosition(surface, x, y);
      }
    }
  };

  DdeShellGlobal(TreelandProtocolManagerImpl* owner, wl_display* display)
      : owner_(owner) {
    if (display != nullptr) {
      global_ =
          wl_global_create(display, &treeland_dde_shell_manager_v1_interface,
                           kManagerVersion, this, Bind);
    }
  }

  ~DdeShellGlobal() override {
    if (global_ != nullptr) wl_global_destroy(global_);
  }

  bool IsValid() const override { return global_ != nullptr; }

 private:
  static void DestroyRequest(wl_client*, wl_resource* resource) {
    wl_resource_destroy(resource);
  }

  static void Bind(wl_client* client, void* data, uint32_t version,
                   uint32_t id) {
    static const struct treeland_dde_shell_manager_v1_interface impl = {
        .get_window_overlap_checker = GetOverlapChecker,
        .get_shell_surface = GetShellSurface,
        .get_treeland_dde_active = GetActive,
        .get_treeland_multitaskview = GetMultitaskView,
        .get_treeland_window_picker = GetWindowPicker,
        .get_treeland_lockscreen = GetLockscreen,
        .set_xwindow_position_relative = SetXWindowPositionRelative,
        .destroy = DestroyRequest,
    };
    wl_resource* resource = wl_resource_create(
        client, &treeland_dde_shell_manager_v1_interface,
        static_cast<int>(std::min(version, kManagerVersion)), id);
    if (resource == nullptr) {
      wl_client_post_no_memory(client);
      return;
    }
    wl_resource_set_implementation(resource, &impl, data, nullptr);
  }

  static void GetShellSurface(wl_client* client, wl_resource* manager_resource,
                              uint32_t id, wl_resource* surface_resource) {
    static const struct treeland_dde_shell_surface_v1_interface impl = {
        .destroy = DestroyRequest,
        .set_surface_position = SetSurfacePosition,
        .set_role = SetRole,
        .set_auto_placement = SetAutoPlacement,
        .set_skip_switcher = SetBoolean,
        .set_skip_dock_preview = SetBoolean,
        .set_skip_muti_task_view = SetBoolean,
        .set_accept_keyboard_focus = SetAcceptFocus,
    };
    auto* shell = static_cast<DdeShellGlobal*>(
        wl_resource_get_user_data(manager_resource));
    wlr_surface* surface = wlr_surface_from_resource(surface_resource);
    if (surface == nullptr) return;
    auto duplicate =
        std::find_if(shell->surfaces_.begin(), shell->surfaces_.end(),
                     [surface](const SurfaceState* state) {
                       return state->surface == surface;
                     });
    if (duplicate != shell->surfaces_.end()) {
      wl_client_post_implementation_error(
          client, "wl_surface already has a Treeland DDE shell surface");
      return;
    }
    wl_resource* resource = wl_resource_create(
        client, &treeland_dde_shell_surface_v1_interface, 1, id);
    if (resource == nullptr) {
      wl_client_post_no_memory(client);
      return;
    }
    auto* state = new SurfaceState(shell, resource, surface);
    state->map.Connect(&surface->events.map);
    state->commit.Connect(&surface->events.commit);
    state->destroy.Connect(&surface->events.destroy);
    shell->surfaces_.push_back(state);
    wl_resource_set_implementation(resource, &impl, state, DestroySurface);
  }

  static void DestroySurface(wl_resource* resource) {
    auto* state =
        static_cast<SurfaceState*>(wl_resource_get_user_data(resource));
    if (state == nullptr) return;
    state->resource = nullptr;
    state->map.Disconnect();
    state->commit.Disconnect();
    state->destroy.Disconnect();
    std::erase(state->shell->surfaces_, state);
    delete state;
  }

  // Parameter order is fixed by the generated Wayland ABI.
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  static void SetSurfacePosition(wl_client*, wl_resource* resource, int32_t x,
                                 int32_t y) {
    auto* state =
        static_cast<SurfaceState*>(wl_resource_get_user_data(resource));
    state->x = x;
    state->y = y;
    state->has_position = true;
    state->auto_placement = false;
    state->Apply();
  }

  static void SetRole(wl_client*, wl_resource* resource, uint32_t role) {
    auto* state =
        static_cast<SurfaceState*>(wl_resource_get_user_data(resource));
    state->overlay = role == TREELAND_DDE_SHELL_SURFACE_V1_ROLE_OVERLAY;
    state->Apply();
  }

  static void SetAutoPlacement(wl_client*, wl_resource* resource,
                               uint32_t y_offset) {
    auto* state =
        static_cast<SurfaceState*>(wl_resource_get_user_data(resource));
    // Auto-placement should be one shot one, freezing after placement is done.
    state->y_offset = static_cast<int32_t>(y_offset);
    if (wlr_cursor* cursor = state->shell->owner_->Cursor();
        cursor != nullptr) {
      state->x = static_cast<int32_t>(cursor->x);
      state->y = static_cast<int32_t>(cursor->y) + state->y_offset;
      state->has_position = true;
      state->auto_placement = false;
    } else {
      // Only if cursor is not avaliable yet.
      state->auto_placement = true;
      state->has_position = false;
    }
    state->Apply();
  }

  static void SetBoolean(wl_client*, wl_resource*, uint32_t) {}

  static void SetAcceptFocus(wl_client*, wl_resource* resource,
                             uint32_t accept) {
    auto* state =
        static_cast<SurfaceState*>(wl_resource_get_user_data(resource));
    state->accept_focus = accept != 0;
  }

  static void GetOverlapChecker(wl_client* client, wl_resource*, uint32_t id) {
    static const struct treeland_window_overlap_checker_interface impl = {
        .update = UpdateOverlap,
        .destroy = DestroyRequest,
    };
    wl_resource* resource = wl_resource_create(
        client, &treeland_window_overlap_checker_interface, 1, id);
    if (resource == nullptr) {
      wl_client_post_no_memory(client);
      return;
    }
    wl_resource_set_implementation(resource, &impl, nullptr, nullptr);
  }

  static void UpdateOverlap(wl_client*, wl_resource* resource, int32_t, int32_t,
                            uint32_t, wl_resource*) {
    treeland_window_overlap_checker_send_leave(resource);
  }

  static void GetActive(wl_client* client, wl_resource*, uint32_t id,
                        wl_resource*) {
    static const struct treeland_dde_active_v1_interface impl = {
        .destroy = DestroyRequest,
    };
    CreateSimpleResource(client, &treeland_dde_active_v1_interface, &impl, id);
  }

  static void GetMultitaskView(wl_client* client, wl_resource*, uint32_t id) {
    static const struct treeland_multitaskview_v1_interface impl = {
        .destroy = DestroyRequest,
        .toggle = NoopRequest,
    };
    CreateSimpleResource(client, &treeland_multitaskview_v1_interface, &impl,
                         id);
  }

  static void GetWindowPicker(wl_client* client, wl_resource* manager_resource,
                              uint32_t id) {
    static const struct treeland_window_picker_v1_interface impl = {
        .destroy = DestroyRequest,
        .pick = PickWindow,
    };
    auto* shell = static_cast<DdeShellGlobal*>(
        wl_resource_get_user_data(manager_resource));
    wl_resource* resource =
        wl_resource_create(client, &treeland_window_picker_v1_interface, 1, id);
    if (resource == nullptr) {
      wl_client_post_no_memory(client);
      return;
    }
    wl_resource_set_implementation(resource, &impl, shell, nullptr);
  }

  static void PickWindow(wl_client*, wl_resource* resource, const char*) {
    auto* shell =
        static_cast<DdeShellGlobal*>(wl_resource_get_user_data(resource));
    pid_t pid = 0;
    wlr_seat* seat = shell->owner_->Seat();
    wlr_surface* surface =
        seat == nullptr ? nullptr : seat->keyboard_state.focused_surface;
    if (surface != nullptr && surface->resource != nullptr) {
      wl_client_get_credentials(wl_resource_get_client(surface->resource), &pid,
                                nullptr, nullptr);
    }
    treeland_window_picker_v1_send_window(resource, pid);
  }

  static void GetLockscreen(wl_client* client, wl_resource*, uint32_t id) {
    static const struct treeland_lockscreen_v1_interface impl = {
        .destroy = DestroyRequest,
        .lock = NoopRequest,
        .shutdown = NoopRequest,
        .switch_user = NoopRequest,
    };
    CreateSimpleResource(client, &treeland_lockscreen_v1_interface, &impl, id);
  }

  static void NoopRequest(wl_client*, wl_resource*) {}

  static void CreateSimpleResource(wl_client* client,
                                   const wl_interface* interface,
                                   const void* implementation, uint32_t id) {
    wl_resource* resource = wl_resource_create(client, interface, 1, id);
    if (resource == nullptr) {
      wl_client_post_no_memory(client);
      return;
    }
    wl_resource_set_implementation(resource, implementation, nullptr, nullptr);
  }

  static void SetXWindowPositionRelative(wl_client* client, wl_resource*,
                                         uint32_t callback_id, uint32_t,
                                         wl_resource*, wl_fixed_t, wl_fixed_t) {
    wl_resource* callback =
        wl_resource_create(client, &wl_callback_interface, 1, callback_id);
    if (callback == nullptr) {
      wl_client_post_no_memory(client);
      return;
    }
    wl_callback_send_done(callback, 1);
    wl_resource_destroy(callback);
  }

  TreelandProtocolManagerImpl* owner_;
  wl_global* global_ = nullptr;
  std::vector<SurfaceState*> surfaces_;
};

}  // namespace

std::unique_ptr<TreelandGlobal> CreateTreelandDdeShellGlobal(
    TreelandProtocolManagerImpl* owner, wl_display* display) {
  return std::make_unique<DdeShellGlobal>(owner, display);
}

}  // namespace protocol
}  // namespace flakewm
