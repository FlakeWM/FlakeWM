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
 * Originally copyright by (C) 2023 KylinSoft Co., Ltd.
 * Original license: GPL-1.0-or-later, see Open Kylin Wayland Compositor.
 * Redistributed with GPL-3.0-or-later.
 * Original code is modified to adapt C++ and Wlroots 0.20.2.
 */

#include <algorithm>
#include <memory>

#include "src/protocol/input_method/input_method_relay/input_method_relay.h"
#include "src/protocol/text_input/text_input_v2/text_input_v2.h"
#include "src/protocol/text_input/text_input_v2_manager/text_input_v2_manager.h"

extern "C" {
#include "protocol/text-input-unstable-v2-protocol.h"
}

namespace flakewm {
namespace protocol {

const struct zwp_text_input_manager_v2_interface
    TextInputV2Manager::kImplementation = {
        .destroy = Destroy,
        .get_text_input = GetTextInput,
};

TextInputV2Manager::TextInputV2Manager(wl_display* display,
                                       InputMethodRelay* relay)
    : relay_(relay) {
  global_ = wl_global_create(display, &zwp_text_input_manager_v2_interface, 1,
                             this, Bind);
}

TextInputV2Manager::~TextInputV2Manager() {
  if (global_ != nullptr) {
    wl_global_destroy(global_);
  }
}

bool TextInputV2Manager::IsValid() const { return global_ != nullptr; }

void TextInputV2Manager::Bind(wl_client* client, void* data, uint32_t version,
                              uint32_t id) {
  auto* manager = static_cast<TextInputV2Manager*>(data);
  wl_resource* resource = wl_resource_create(
      client, &zwp_text_input_manager_v2_interface, std::min(version, 1U), id);
  if (resource == nullptr) {
    wl_client_post_no_memory(client);
    return;
  }
  wl_resource_set_implementation(resource, &kImplementation, manager, nullptr);
}

void TextInputV2Manager::Destroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void TextInputV2Manager::GetTextInput(wl_client* client, wl_resource* resource,
                                      uint32_t id, wl_resource* seat) {
  auto* manager =
      static_cast<TextInputV2Manager*>(wl_resource_get_user_data(resource));
  wlr_seat_client* seat_client = wlr_seat_client_from_resource(seat);
  if (manager == nullptr || seat_client == nullptr) {
    return;
  }

  wl_resource* input_resource =
      wl_resource_create(client, &zwp_text_input_v2_interface,
                         wl_resource_get_version(resource), id);
  if (input_resource == nullptr) {
    wl_client_post_no_memory(client);
    return;
  }
  manager->relay_->AddTextInput(
      std::make_unique<TextInputV2>(input_resource, seat_client));
}

}  // namespace protocol
}  // namespace flakewm
