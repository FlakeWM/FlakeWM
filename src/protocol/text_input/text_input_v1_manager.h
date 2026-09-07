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

#ifndef SRC_PROTOCOL_TEXT_INPUT_TEXT_INPUT_V1_MANAGER_TEXT_INPUT_V1_MANAGER_H_
#define SRC_PROTOCOL_TEXT_INPUT_TEXT_INPUT_V1_MANAGER_TEXT_INPUT_V1_MANAGER_H_

#include "src/wlr_wrapper/wlroots.h"

struct zwp_text_input_manager_v1_interface;

namespace flakewm {
namespace protocol {

class InputMethodRelay;

// Publishes the legacy text-input-v1 global.
class TextInputV1Manager final {
 public:
  TextInputV1Manager(wl_display* display, InputMethodRelay* relay);
  ~TextInputV1Manager();

  TextInputV1Manager(const TextInputV1Manager&) = delete;
  TextInputV1Manager& operator=(const TextInputV1Manager&) = delete;

  bool IsValid() const;

 private:
  static void Bind(wl_client* client, void* data, uint32_t version,
                   uint32_t id);
  static void CreateTextInput(wl_client* client, wl_resource* resource,
                              uint32_t id);

  static const struct zwp_text_input_manager_v1_interface kImplementation;

  wl_global* global_ = nullptr;
  InputMethodRelay* relay_ = nullptr;
};

}  // namespace protocol
}  // namespace flakewm

#endif  // SRC_PROTOCOL_TEXT_INPUT_TEXT_INPUT_V1_MANAGER_TEXT_INPUT_V1_MANAGER_H_
