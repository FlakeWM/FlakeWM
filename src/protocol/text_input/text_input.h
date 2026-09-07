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

#ifndef SRC_PROTOCOL_TEXT_INPUT_TEXT_INPUT_TEXT_INPUT_H_
#define SRC_PROTOCOL_TEXT_INPUT_TEXT_INPUT_TEXT_INPUT_H_

#include <cstdint>
#include <string_view>

#include "src/wlr_wrapper/wlroots.h"

namespace flakewm {
namespace protocol {

class InputMethodRelay;

// A version-neutral text-input endpoint used by the input method relay.
class TextInput {
 public:
  struct State {
    std::string_view surrounding_text;
    uint32_t surrounding_cursor = 0;
    uint32_t surrounding_anchor = 0;
    uint32_t content_hint = 0;
    uint32_t content_purpose = 0;
    uint32_t text_change_cause = 0;
    wlr_box cursor_rectangle = {};
    bool has_surrounding_text = false;
    bool has_content_type = false;
    bool has_cursor_rectangle = false;
  };

  virtual ~TextInput();

  TextInput(const TextInput&) = delete;
  TextInput& operator=(const TextInput&) = delete;

  virtual wl_client* Client() const = 0;
  virtual wlr_seat* Seat() const = 0;
  virtual wlr_surface* FocusedSurface() const = 0;
  virtual bool MatchesSurface(wlr_surface* surface) const = 0;
  virtual bool Enabled() const = 0;
  virtual State CurrentState() const = 0;

  virtual void SendEnter(wlr_surface* surface) = 0;
  virtual void SendLeave() = 0;
  virtual void SendPreedit(std::string_view text, int32_t cursor_begin,
                           int32_t cursor_end) = 0;
  virtual void SendCommit(std::string_view text) = 0;
  virtual void SendDelete(uint32_t before_length, uint32_t after_length) = 0;
  virtual void SendDone() = 0;

  void AttachRelay(InputMethodRelay* relay);
  wlr_surface* RelayFocusedSurface() const;
  void SetRelayFocusedSurface(wlr_surface* surface);

 protected:
  TextInput() = default;

  void NotifyEnable();
  void NotifyCommit();
  void NotifyDisable();
  void NotifyDestroy();

 private:
  InputMethodRelay* relay_ = nullptr;
  wlr_surface* relay_focused_surface_ = nullptr;
};

}  // namespace protocol
}  // namespace flakewm

#endif  // SRC_PROTOCOL_TEXT_INPUT_TEXT_INPUT_TEXT_INPUT_H_
