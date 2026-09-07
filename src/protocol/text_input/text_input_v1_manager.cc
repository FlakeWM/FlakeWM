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

#include "src/protocol/input_method/input_method_relay.h"
#include "src/protocol/text_input/text_input_v1.h"
#include "src/protocol/text_input/text_input_v1_manager.h"

extern "C" {
#include "protocol/text-input-unstable-v1-protocol.h"
}

namespace flakewm {
namespace protocol {

const struct zwp_text_input_manager_v1_interface
    TextInputV1Manager::kImplementation = {
        .create_text_input = CreateTextInput,
};

TextInputV1Manager::TextInputV1Manager(wl_display* display,
                                       InputMethodRelay* relay)
    : relay_(relay) {
  global_ = wl_global_create(display, &zwp_text_input_manager_v1_interface, 1,
                             this, Bind);
}

TextInputV1Manager::~TextInputV1Manager() {
  if (global_ != nullptr) {
    wl_global_destroy(global_);
  }
}

bool TextInputV1Manager::IsValid() const { return global_ != nullptr; }

void TextInputV1Manager::Bind(wl_client* client, void* data, uint32_t version,
                              uint32_t id) {
  auto* manager = static_cast<TextInputV1Manager*>(data);
  wl_resource* resource = wl_resource_create(
      client, &zwp_text_input_manager_v1_interface, std::min(version, 1U), id);
  if (resource == nullptr) {
    wl_client_post_no_memory(client);
    return;
  }
  wl_resource_set_implementation(resource, &kImplementation, manager, nullptr);
}

void TextInputV1Manager::CreateTextInput(wl_client* client,
                                         wl_resource* resource, uint32_t id) {
  auto* manager =
      static_cast<TextInputV1Manager*>(wl_resource_get_user_data(resource));
  wl_resource* input_resource =
      wl_resource_create(client, &zwp_text_input_v1_interface,
                         wl_resource_get_version(resource), id);
  if (manager == nullptr || input_resource == nullptr) {
    wl_client_post_no_memory(client);
    return;
  }
  manager->relay_->AddTextInput(std::make_unique<TextInputV1>(input_resource));
}

}  // namespace protocol
}  // namespace flakewm
