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
#include <string>

#include "src/protocol/text_input/text_input_v1/text_input_v1.h"

extern "C" {
#include "protocol/text-input-unstable-v1-protocol.h"
}

namespace flakewm {
namespace protocol {

const struct zwp_text_input_v1_interface TextInputV1::kImplementation = {
    .activate = Activate,
    .deactivate = Deactivate,
    .show_input_panel = ShowInputPanel,
    .hide_input_panel = HideInputPanel,
    .reset = Reset,
    .set_surrounding_text = SetSurroundingText,
    .set_content_type = SetContentType,
    .set_cursor_rectangle = SetCursorRectangle,
    .set_preferred_language = SetPreferredLanguage,
    .commit_state = CommitState,
    .invoke_action = InvokeAction,
};

TextInputV1::TextInputV1(wl_resource* resource) : resource_(resource) {
  wl_resource_set_implementation(resource_, &kImplementation, this,
                                 OnResourceDestroy);
}

TextInputV1::~TextInputV1() { MakeInert(); }

wl_client* TextInputV1::Client() const {
  return resource_ == nullptr ? nullptr : wl_resource_get_client(resource_);
}

wlr_seat* TextInputV1::Seat() const { return seat_; }

wlr_surface* TextInputV1::FocusedSurface() const { return surface_; }

bool TextInputV1::MatchesSurface(wlr_surface* surface) const {
  return surface_ != nullptr && surface_ == surface;
}

bool TextInputV1::Enabled() const { return activated_; }

TextInput::State TextInputV1::CurrentState() const {
  return {
      .surrounding_text = surrounding_text_,
      .surrounding_cursor = surrounding_cursor_,
      .surrounding_anchor = surrounding_anchor_,
      .content_hint = content_hint_,
      .content_purpose = content_purpose_,
      .cursor_rectangle = cursor_rectangle_,
      .has_surrounding_text = surrounding_pending_,
      .has_content_type = content_type_pending_,
      .has_cursor_rectangle = cursor_rectangle_pending_,
  };
}

void TextInputV1::SendEnter(wlr_surface* surface) {
  if (resource_ == nullptr || surface == nullptr || surface_ != surface) {
    return;
  }
  zwp_text_input_v1_send_enter(resource_, surface->resource);
}

void TextInputV1::SendLeave() {
  if (resource_ == nullptr || surface_ == nullptr) {
    return;
  }
  zwp_text_input_v1_send_leave(resource_);
  surface_destroy_.Disconnect();
  surface_ = nullptr;
}

void TextInputV1::SendPreedit(std::string_view text, int32_t cursor_begin,
                              int32_t) {
  if (resource_ == nullptr) {
    return;
  }
  const std::string value(text);
  zwp_text_input_v1_send_preedit_cursor(resource_, cursor_begin);
  zwp_text_input_v1_send_preedit_styling(
      resource_, 0, static_cast<uint32_t>(value.size()),
      ZWP_TEXT_INPUT_V1_PREEDIT_STYLE_DEFAULT);
  zwp_text_input_v1_send_preedit_string(resource_, serial_, value.c_str(), "");
}

void TextInputV1::SendCommit(std::string_view text) {
  if (resource_ == nullptr) {
    return;
  }
  const std::string value(text);
  zwp_text_input_v1_send_commit_string(resource_, serial_, value.c_str());
}

void TextInputV1::SendDelete(uint32_t before_length, uint32_t after_length) {
  if (resource_ == nullptr) {
    return;
  }
  const uint32_t size = static_cast<uint32_t>(surrounding_text_.size());
  const uint32_t index = size > before_length ? size - before_length : 0;
  zwp_text_input_v1_send_delete_surrounding_text(resource_, index,
                                                 before_length + after_length);
}

void TextInputV1::SendDone() {}

TextInputV1* TextInputV1::FromResource(wl_resource* resource) {
  return static_cast<TextInputV1*>(wl_resource_get_user_data(resource));
}

void TextInputV1::OnResourceDestroy(wl_resource* resource) {
  TextInputV1* input = FromResource(resource);
  if (input == nullptr) {
    return;
  }
  input->resource_ = nullptr;
  input->NotifyDestroy();
}

void TextInputV1::OnSeatDestroy(TextInputV1* input, void*) {
  input->MakeInert();
  input->NotifyDestroy();
}

void TextInputV1::OnSurfaceDestroy(TextInputV1* input, void*) {
  input->MakeInert();
  input->NotifyDestroy();
}

void TextInputV1::Activate(wl_client* client, wl_resource* resource,
                           wl_resource* seat, wl_resource* surface) {
  TextInputV1* input = FromResource(resource);
  wlr_seat_client* seat_client = wlr_seat_client_from_resource(seat);
  if (input == nullptr || seat_client == nullptr || surface == nullptr ||
      wl_resource_get_client(surface) != client) {
    return;
  }

  input->seat_destroy_.Disconnect();
  input->surface_destroy_.Disconnect();
  input->seat_ = seat_client->seat;
  input->surface_ = wlr_surface_from_resource(surface);
  input->seat_destroy_.Connect(&seat_client->events.destroy);
  input->surface_destroy_.Connect(&input->surface_->events.destroy);
  input->activated_ = true;
  input->NotifyEnable();
}

void TextInputV1::Deactivate(wl_client*, wl_resource* resource,
                             wl_resource* seat) {
  TextInputV1* input = FromResource(resource);
  wlr_seat_client* seat_client = wlr_seat_client_from_resource(seat);
  if (input == nullptr || seat_client == nullptr ||
      input->seat_ != seat_client->seat) {
    return;
  }

  input->activated_ = false;
  input->NotifyDisable();
  input->seat_destroy_.Disconnect();
  input->surface_destroy_.Disconnect();
  input->seat_ = nullptr;
  input->surface_ = nullptr;
}

void TextInputV1::ShowInputPanel(wl_client*, wl_resource*) {}

void TextInputV1::HideInputPanel(wl_client*, wl_resource*) {}

void TextInputV1::Reset(wl_client*, wl_resource* resource) {
  TextInputV1* input = FromResource(resource);
  if (input == nullptr) {
    return;
  }
  input->surrounding_pending_ = false;
  input->content_type_pending_ = false;
}

void TextInputV1::SetSurroundingText(wl_client*, wl_resource* resource,
                                     const char* text, uint32_t cursor,
                                     uint32_t anchor) {
  TextInputV1* input = FromResource(resource);
  if (input == nullptr) {
    return;
  }
  input->surrounding_text_ = text == nullptr ? "" : text;
  input->surrounding_cursor_ = cursor;
  input->surrounding_anchor_ = anchor;
  input->surrounding_pending_ = true;
}

void TextInputV1::SetContentType(wl_client*, wl_resource* resource,
                                 uint32_t hint, uint32_t purpose) {
  TextInputV1* input = FromResource(resource);
  if (input == nullptr) {
    return;
  }
  // v1's DEFAULT is a bundle of flags; input-method-v2 expects those flags.
  input->content_hint_ = hint == ZWP_TEXT_INPUT_V1_CONTENT_HINT_DEFAULT
                             ? ZWP_TEXT_INPUT_V1_CONTENT_HINT_NONE
                             : hint;
  input->content_purpose_ = purpose > ZWP_TEXT_INPUT_V1_CONTENT_PURPOSE_PASSWORD
                                ? purpose + 1
                                : purpose;
  input->content_type_pending_ = true;
}

void TextInputV1::SetCursorRectangle(wl_client*, wl_resource* resource,
                                     int32_t x, int32_t y, int32_t width,
                                     int32_t height) {
  TextInputV1* input = FromResource(resource);
  if (input == nullptr) {
    return;
  }
  input->cursor_rectangle_ = {x, y, width, height};
  input->cursor_rectangle_pending_ = true;
}

void TextInputV1::SetPreferredLanguage(wl_client*, wl_resource*, const char*) {}

void TextInputV1::CommitState(wl_client*, wl_resource* resource,
                              uint32_t serial) {
  TextInputV1* input = FromResource(resource);
  if (input == nullptr) {
    return;
  }
  input->serial_ = serial;
  input->NotifyCommit();
}

void TextInputV1::InvokeAction(wl_client*, wl_resource*, uint32_t, uint32_t) {}

void TextInputV1::MakeInert() {
  seat_destroy_.Disconnect();
  surface_destroy_.Disconnect();
  seat_ = nullptr;
  surface_ = nullptr;
  activated_ = false;
  if (resource_ != nullptr) {
    wl_resource_set_user_data(resource_, nullptr);
    resource_ = nullptr;
  }
}

}  // namespace protocol
}  // namespace flakewm
