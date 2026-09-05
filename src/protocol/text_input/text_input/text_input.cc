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

#include "src/protocol/input_method/input_method_relay/input_method_relay.h"
#include "src/protocol/text_input/text_input/text_input.h"

namespace flakewm {
namespace protocol {

TextInput::~TextInput() = default;

void TextInput::AttachRelay(InputMethodRelay* relay) { relay_ = relay; }

wlr_surface* TextInput::RelayFocusedSurface() const {
  return relay_focused_surface_;
}

void TextInput::SetRelayFocusedSurface(wlr_surface* surface) {
  relay_focused_surface_ = surface;
}

void TextInput::NotifyEnable() {
  if (relay_ != nullptr) {
    relay_->HandleEnable(this);
  }
}

void TextInput::NotifyCommit() {
  if (relay_ != nullptr) {
    relay_->HandleCommit(this);
  }
}

void TextInput::NotifyDisable() {
  if (relay_ != nullptr) {
    relay_->HandleDisable(this);
  }
}

void TextInput::NotifyDestroy() {
  if (relay_ != nullptr) {
    InputMethodRelay* relay = relay_;
    relay_ = nullptr;
    relay->HandleDestroy(this);
  }
}

}  // namespace protocol
}  // namespace flakewm
