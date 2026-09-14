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
 * This is an implementation of GXDE Identifier.
 */

#include "protocol/gxde-identifier-v1-protocol.h"
#include "src/protocol/gxde/gxde_protocol_manager_internal.h"

namespace flakewm {
namespace protocol {
namespace {

constexpr char kGxdeCompositorVersion[] = "3.0";

void DestroyIdentifier(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void BindIdentifier(wl_client* client, void*, uint32_t version, uint32_t id) {
  static const struct gxde_identifier_v1_interface implementation = {
      .destroy = DestroyIdentifier,
  };
  wl_resource* resource = wl_resource_create(
      client, &gxde_identifier_v1_interface, static_cast<int>(version), id);
  if (resource == nullptr) {
    wl_client_post_no_memory(client);
    return;
  }
  wl_resource_set_implementation(resource, &implementation, nullptr, nullptr);
  gxde_identifier_v1_send_version(resource, kGxdeCompositorVersion);
}

}  // namespace

wl_global* CreateGxdeIdentifierGlobal(wl_display* display) {
  return display == nullptr
             ? nullptr
             : wl_global_create(display, &gxde_identifier_v1_interface, 1,
                                nullptr, BindIdentifier);
}

}  // namespace protocol
}  // namespace flakewm
