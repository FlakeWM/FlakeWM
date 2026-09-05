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

#ifndef SRC_PROTOCOL_TEXT_INPUT_TEXT_INPUT_V3_TEXT_INPUT_V3_H_
#define SRC_PROTOCOL_TEXT_INPUT_TEXT_INPUT_V3_TEXT_INPUT_V3_H_

#include <cstdint>
#include <string_view>

#include "src/protocol/text_input/text_input/text_input.h"
#include "src/utils/signal_listener.h"

namespace flakewm {
namespace protocol {

// Adapts wlroots text-input-v3 to the common relay interface.
class TextInputV3 final : public TextInput {
 public:
  explicit TextInputV3(wlr_text_input_v3* handle);
  ~TextInputV3() override;

  wl_client* Client() const override;
  wlr_seat* Seat() const override;
  wlr_surface* FocusedSurface() const override;
  bool MatchesSurface(wlr_surface* surface) const override;
  bool Enabled() const override;
  State CurrentState() const override;

  void SendEnter(wlr_surface* surface) override;
  void SendLeave() override;
  void SendPreedit(std::string_view text, int32_t cursor_begin,
                   int32_t cursor_end) override;
  void SendCommit(std::string_view text) override;
  void SendDelete(uint32_t before_length, uint32_t after_length) override;
  void SendDone() override;

 private:
  static void OnEnable(TextInputV3* input, void*);
  static void OnCommit(TextInputV3* input, void*);
  static void OnDisable(TextInputV3* input, void*);
  static void OnDestroy(TextInputV3* input, void*);

  wlr_text_input_v3* handle_ = nullptr;
  utils::SignalListener<TextInputV3, void> enable_{this, OnEnable};
  utils::SignalListener<TextInputV3, void> commit_{this, OnCommit};
  utils::SignalListener<TextInputV3, void> disable_{this, OnDisable};
  utils::SignalListener<TextInputV3, void> destroy_{this, OnDestroy};
};

}  // namespace protocol
}  // namespace flakewm

#endif  // SRC_PROTOCOL_TEXT_INPUT_TEXT_INPUT_V3_TEXT_INPUT_V3_H_
