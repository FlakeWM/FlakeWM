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
 * Originally copyright by (C) UnionTech Software Technology Co., Ltd.
 * Original license: GPL-2.0-or-later.
 * Redistributed with GPL-3.0-or-later.
 * Original code is modified to adapt C++ and Wlroots 0.20.2.
 */

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "protocol/dde-shell-protocol.h"
#include "src/protocol/treeland/treeland_protocol_manager_internal.h"
#include "src/utils/signal_listener.h"

namespace flakewm {
namespace protocol {
namespace {

constexpr uint32_t kVersion = 2;

class LegacyDdeShellGlobal final : public TreelandGlobal {
 public:
  struct SurfaceState {
    SurfaceState(LegacyDdeShellGlobal* new_shell, wl_resource* new_resource,
                 wlr_surface* new_surface)
        : shell(new_shell), resource(new_resource), surface(new_surface) {}

    LegacyDdeShellGlobal* shell;
    wl_resource* resource;
    wlr_surface* surface;
    float radius_x = -1.0F;
    float radius_y = -1.0F;
    bool no_titlebar = false;
    uint32_t effects = 0;
    uint32_t startup_effect = 0;
    utils::SignalListener<SurfaceState, void> map{this, OnMap};
    utils::SignalListener<SurfaceState, void> destroy{this, OnSurfaceDestroy};

    static void OnMap(SurfaceState* state, void*) { state->Apply(); }
    static void OnSurfaceDestroy(SurfaceState* state, void*) {
      state->map.Disconnect();
      state->destroy.Disconnect();
      state->surface = nullptr;
      if (state->resource != nullptr) wl_resource_destroy(state->resource);
    }
    void Apply() const {
      if (surface == nullptr) return;
      if (no_titlebar) shell->owner_->SetTitlebar(surface, false);
      const bool no_radius =
          (effects & DDE_SHELL_EFFECTSCENE_EFFECTNORADIUS) != 0;
      if (radius_x >= 0.0F || no_radius) {
        shell->owner_->SetRoundCorner(
            surface, no_radius ? 0 : static_cast<int>(radius_x + 0.5F));
      }
      const bool no_shadow =
          (effects & DDE_SHELL_EFFECTSCENE_EFFECTNOSHADOW) != 0;

      // DTK2/5 never transmit ShadowRadius/ShadowOffset/ShadowColor, but only
      // presents NoTitleBar & WindowRadius.
      shell->owner_->SetShadow(surface, no_titlebar && !no_shadow);
    }
  };

  LegacyDdeShellGlobal(TreelandProtocolManagerImpl* owner, wl_display* display)
      : owner_(owner) {
    if (display != nullptr) {
      global_ =
          wl_global_create(display, &dde_shell_interface, kVersion, this, Bind);
    }
  }

  ~LegacyDdeShellGlobal() override {
    if (global_ != nullptr) wl_global_destroy(global_);
  }

  bool IsValid() const override { return global_ != nullptr; }

 private:
  static void Bind(wl_client* client, void* data, uint32_t version,
                   uint32_t id) {
    static const struct dde_shell_interface impl = {
        .get_shell_surface = GetShellSurface,
    };
    wl_resource* resource =
        wl_resource_create(client, &dde_shell_interface,
                           static_cast<int>(std::min(version, kVersion)), id);
    if (resource == nullptr) {
      wl_client_post_no_memory(client);
      return;
    }
    wl_resource_set_implementation(resource, &impl, data, nullptr);
  }

  static void GetShellSurface(wl_client* client, wl_resource* shell_resource,
                              uint32_t id, wl_resource* surface_resource) {
    static const struct dde_shell_surface_interface impl = {
        .get_geometry = GetGeometry,
        .request_active = RequestActive,
        .set_state = SetState,
        .set_property = SetProperty,
        .request_split_window = RequestSplitWindow,
        .request_window_effect = RequestWindowEffect,
        .request_window_startup_effect = RequestWindowStartupEffect,
    };
    auto* shell = static_cast<LegacyDdeShellGlobal*>(
        wl_resource_get_user_data(shell_resource));
    wlr_surface* surface = wlr_surface_from_resource(surface_resource);
    if (surface == nullptr) return;
    wl_resource* resource =
        wl_resource_create(client, &dde_shell_surface_interface,
                           wl_resource_get_version(shell_resource), id);
    if (resource == nullptr) {
      wl_client_post_no_memory(client);
      return;
    }
    auto* state = new SurfaceState(shell, resource, surface);
    state->map.Connect(&surface->events.map);
    state->destroy.Connect(&surface->events.destroy);
    shell->surfaces_.push_back(state);
    wl_resource_set_implementation(resource, &impl, state, DestroySurface);
    if (surface->mapped) state->Apply();
  }

  static void DestroySurface(wl_resource* resource) {
    auto* state =
        static_cast<SurfaceState*>(wl_resource_get_user_data(resource));
    if (state == nullptr) return;
    state->resource = nullptr;
    state->map.Disconnect();
    state->destroy.Disconnect();
    std::erase(state->shell->surfaces_, state);
    delete state;
  }

  static void GetGeometry(wl_client*, wl_resource* resource) {
    auto* state =
        static_cast<SurfaceState*>(wl_resource_get_user_data(resource));
    wlr_box box = {};
    if (state->shell->owner_->SurfaceGeometry(state->surface, &box)) {
      dde_shell_surface_send_geometry(resource, box.x, box.y, box.width,
                                      box.height);
    }
  }

  static void RequestActive(wl_client*, wl_resource* resource) {
    auto* state =
        static_cast<SurfaceState*>(wl_resource_get_user_data(resource));
    state->shell->owner_->ActivateSurface(state->surface);
  }

  static void SetState(wl_client*, wl_resource* resource, uint32_t flags,
                       uint32_t state_flags) {
    auto* state =
        static_cast<SurfaceState*>(wl_resource_get_user_data(resource));
    auto set = [flags, state_flags](uint32_t bit) {
      return (flags & bit) != 0 && (state_flags & bit) != 0;
    };
    if ((flags & DDE_SHELL_STATE_ACTIVE) != 0 &&
        (state_flags & DDE_SHELL_STATE_ACTIVE) != 0) {
      state->shell->owner_->ActivateSurface(state->surface);
    }
    if ((flags & DDE_SHELL_STATE_MINIMIZED) != 0) {
      state->shell->owner_->SetMinimized(state->surface,
                                         set(DDE_SHELL_STATE_MINIMIZED));
    }
    if ((flags & DDE_SHELL_STATE_MAXIMIZED) != 0) {
      state->shell->owner_->SetMaximized(state->surface,
                                         set(DDE_SHELL_STATE_MAXIMIZED));
    }
    if ((flags & DDE_SHELL_STATE_FULLSCREEN) != 0) {
      state->shell->owner_->SetFullscreen(state->surface,
                                          set(DDE_SHELL_STATE_FULLSCREEN));
    }
    dde_shell_surface_send_state_changed(resource, state_flags);
  }

  static void SetProperty(wl_client*, wl_resource* resource, uint32_t property,
                          wl_array* data) {
    auto* state =
        static_cast<SurfaceState*>(wl_resource_get_user_data(resource));
    if (data == nullptr) return;
    if ((property & DDE_SHELL_PROPERTY_NOTITLEBAR) != 0 &&
        data->size >= sizeof(int32_t)) {
      int32_t value = 0;
      std::memcpy(&value, data->data, sizeof(value));
      state->no_titlebar = value != 0;
      state->shell->owner_->SetTitlebar(state->surface, !state->no_titlebar);
    }
    if ((property & DDE_SHELL_PROPERTY_WINDOWRADIUS) != 0 &&
        data->size >= sizeof(float) * 2) {
      float values[2] = {};
      std::memcpy(values, data->data, sizeof(values));
      state->radius_x = values[0];
      state->radius_y = values[1];
      state->Apply();
    }
  }

  static void RequestSplitWindow(wl_client*, wl_resource* resource,
                                 uint32_t split_type) {
    auto* state =
        static_cast<SurfaceState*>(wl_resource_get_user_data(resource));
    if ((split_type & DDE_SHELL_SPLIT_TYPE_LEFT_SPLIT) != 0) {
      state->shell->owner_->SplitSurface(state->surface, true);
    } else if ((split_type & DDE_SHELL_SPLIT_TYPE_RIGHT_SPLIT) != 0) {
      state->shell->owner_->SplitSurface(state->surface, false);
    }
  }

  static void RequestWindowEffect(wl_client*, wl_resource* resource,
                                  uint32_t effects) {
    auto* state =
        static_cast<SurfaceState*>(wl_resource_get_user_data(resource));
    state->effects = effects;
    state->Apply();
  }

  static void RequestWindowStartupEffect(wl_client*, wl_resource* resource,
                                         uint32_t effect) {
    auto* state =
        static_cast<SurfaceState*>(wl_resource_get_user_data(resource));
    state->startup_effect = effect;
  }

  TreelandProtocolManagerImpl* owner_;
  wl_global* global_ = nullptr;
  std::vector<SurfaceState*> surfaces_;
};

}  // namespace

std::unique_ptr<TreelandGlobal> CreateLegacyDdeShellGlobal(
    TreelandProtocolManagerImpl* owner, wl_display* display) {
  return std::make_unique<LegacyDdeShellGlobal>(owner, display);
}

}  // namespace protocol
}  // namespace flakewm
