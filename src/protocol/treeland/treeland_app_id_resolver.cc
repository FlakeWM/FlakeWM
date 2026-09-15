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

#include "protocol/treeland-app-id-resolver-v1-protocol.h"
#include "src/protocol/treeland/treeland_protocol_manager_internal.h"

namespace flakewm {
namespace protocol {
namespace {

constexpr uint32_t kVersion = 1;

class AppIdResolverGlobal final : public TreelandGlobal {
 public:
  AppIdResolverGlobal(TreelandProtocolManagerImpl* owner, wl_display* display)
      : owner_(owner) {
    if (display != nullptr) {
      global_ = wl_global_create(display,
                                 &treeland_app_id_resolver_manager_v1_interface,
                                 kVersion, this, Bind);
    }
  }

  ~AppIdResolverGlobal() override {
    if (global_ != nullptr) wl_global_destroy(global_);
  }

  bool IsValid() const override { return global_ != nullptr; }

 private:
  static void Bind(wl_client* client, void* data, uint32_t version,
                   uint32_t id) {
    static const struct treeland_app_id_resolver_manager_v1_interface impl = {
        .destroy = DestroyRequest,
        .get_resolver = GetResolver,
    };
    auto* self = static_cast<AppIdResolverGlobal*>(data);
    wl_resource* resource = wl_resource_create(
        client, &treeland_app_id_resolver_manager_v1_interface,
        static_cast<int>(std::min(version, kVersion)), id);
    if (resource == nullptr) {
      wl_client_post_no_memory(client);
      return;
    }
    wl_resource_set_implementation(resource, &impl, self, nullptr);
  }

  static void DestroyRequest(wl_client*, wl_resource* resource) {
    wl_resource_destroy(resource);
  }

  static void GetResolver(wl_client* client, wl_resource* manager_resource,
                          uint32_t id) {
    static const struct treeland_app_id_resolver_v1_interface impl = {
        .respond = Respond,
        .destroy = DestroyRequest,
    };
    auto* self = static_cast<AppIdResolverGlobal*>(
        wl_resource_get_user_data(manager_resource));
    if (self->resolver_ != nullptr) {
      wl_resource_post_error(
          manager_resource,
          TREELAND_APP_ID_RESOLVER_MANAGER_V1_ERROR_RESOLVER_ALREADY_EXISTS,
          "an app-id resolver is already registered");
      return;
    }
    wl_resource* resource =
        wl_resource_create(client, &treeland_app_id_resolver_v1_interface,
                           wl_resource_get_version(manager_resource), id);
    if (resource == nullptr) {
      wl_client_post_no_memory(client);
      return;
    }
    self->resolver_ = resource;
    wl_resource_set_implementation(resource, &impl, self, DestroyResolver);
  }

  static void Respond(wl_client*, wl_resource*, uint32_t, const char*,
                      const char*) {
    // FlakeWM currently gets application IDs from xdg-shell/XWayland. Keep
    // the responder available so the privileged GXDE helper can register
    // without a protocol failure; requests are added when pidfd resolution is
    // needed by a compositor feature.
  }

  static void DestroyResolver(wl_resource* resource) {
    auto* self =
        static_cast<AppIdResolverGlobal*>(wl_resource_get_user_data(resource));
    if (self != nullptr && self->resolver_ == resource) {
      self->resolver_ = nullptr;
    }
  }

  TreelandProtocolManagerImpl* owner_;
  wl_global* global_ = nullptr;
  wl_resource* resolver_ = nullptr;
};

}  // namespace

std::unique_ptr<TreelandGlobal> CreateTreelandAppIdResolverGlobal(
    TreelandProtocolManagerImpl* owner, wl_display* display) {
  return std::make_unique<AppIdResolverGlobal>(owner, display);
}

}  // namespace protocol
}  // namespace flakewm
