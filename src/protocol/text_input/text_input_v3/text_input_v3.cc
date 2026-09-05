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

#include <string>

#include "src/protocol/text_input/text_input_v3/text_input_v3.h"

namespace flakewm {
namespace protocol {

TextInputV3::TextInputV3(wlr_text_input_v3* handle) : handle_(handle) {
  enable_.Connect(&handle_->events.enable);
  commit_.Connect(&handle_->events.commit);
  disable_.Connect(&handle_->events.disable);
  destroy_.Connect(&handle_->events.destroy);
}

TextInputV3::~TextInputV3() = default;

wl_client* TextInputV3::Client() const {
  return handle_ == nullptr ? nullptr
                            : wl_resource_get_client(handle_->resource);
}

wlr_seat* TextInputV3::Seat() const {
  return handle_ == nullptr ? nullptr : handle_->seat;
}

wlr_surface* TextInputV3::FocusedSurface() const {
  return handle_ == nullptr ? nullptr : handle_->focused_surface;
}

bool TextInputV3::MatchesSurface(wlr_surface* surface) const {
  return surface != nullptr && Client() != nullptr &&
         Client() == wl_resource_get_client(surface->resource);
}

bool TextInputV3::Enabled() const {
  return handle_ != nullptr && handle_->current_enabled;
}

TextInput::State TextInputV3::CurrentState() const {
  if (handle_ == nullptr) {
    return {};
  }
  const wlr_text_input_v3_state& state = handle_->current;
  return {
      .surrounding_text = state.surrounding.text == nullptr
                              ? std::string_view()
                              : std::string_view(state.surrounding.text),
      .surrounding_cursor = state.surrounding.cursor,
      .surrounding_anchor = state.surrounding.anchor,
      .content_hint = state.content_type.hint,
      .content_purpose = state.content_type.purpose,
      .text_change_cause = state.text_change_cause,
      .cursor_rectangle = state.cursor_rectangle,
      .has_surrounding_text = (handle_->active_features &
                               WLR_TEXT_INPUT_V3_FEATURE_SURROUNDING_TEXT) != 0,
      .has_content_type = (handle_->active_features &
                           WLR_TEXT_INPUT_V3_FEATURE_CONTENT_TYPE) != 0,
      .has_cursor_rectangle =
          (state.features & WLR_TEXT_INPUT_V3_FEATURE_CURSOR_RECTANGLE) != 0,
  };
}

void TextInputV3::SendEnter(wlr_surface* surface) {
  if (handle_ != nullptr && surface != nullptr) {
    wlr_text_input_v3_send_enter(handle_, surface);
  }
}

void TextInputV3::SendLeave() {
  if (handle_ != nullptr && handle_->focused_surface != nullptr) {
    wlr_text_input_v3_send_leave(handle_);
  }
}

void TextInputV3::SendPreedit(std::string_view text, int32_t cursor_begin,
                              int32_t cursor_end) {
  if (handle_ == nullptr) {
    return;
  }
  const std::string value(text);
  wlr_text_input_v3_send_preedit_string(handle_, value.c_str(), cursor_begin,
                                        cursor_end);
}

void TextInputV3::SendCommit(std::string_view text) {
  if (handle_ == nullptr) {
    return;
  }
  const std::string value(text);
  wlr_text_input_v3_send_commit_string(handle_, value.c_str());
}

void TextInputV3::SendDelete(uint32_t before_length, uint32_t after_length) {
  if (handle_ != nullptr) {
    wlr_text_input_v3_send_delete_surrounding_text(handle_, before_length,
                                                   after_length);
  }
}

void TextInputV3::SendDone() {
  if (handle_ != nullptr) {
    wlr_text_input_v3_send_done(handle_);
  }
}

void TextInputV3::OnEnable(TextInputV3* input, void*) { input->NotifyEnable(); }

void TextInputV3::OnCommit(TextInputV3* input, void*) { input->NotifyCommit(); }

void TextInputV3::OnDisable(TextInputV3* input, void*) {
  input->NotifyDisable();
}

void TextInputV3::OnDestroy(TextInputV3* input, void*) {
  input->enable_.Disconnect();
  input->commit_.Disconnect();
  input->disable_.Disconnect();
  input->destroy_.Disconnect();
  input->handle_ = nullptr;
  input->NotifyDestroy();
}

}  // namespace protocol
}  // namespace flakewm
