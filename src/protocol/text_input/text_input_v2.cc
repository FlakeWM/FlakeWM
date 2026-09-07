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

#include "src/protocol/text_input/text_input_v2.h"

extern "C" {
#include "protocol/text-input-unstable-v2-protocol.h"
}

namespace flakewm {
namespace protocol {

const struct zwp_text_input_v2_interface TextInputV2::kImplementation = {
    .destroy = Destroy,
    .enable = Enable,
    .disable = Disable,
    .show_input_panel = ShowInputPanel,
    .hide_input_panel = HideInputPanel,
    .set_surrounding_text = SetSurroundingText,
    .set_content_type = SetContentType,
    .set_cursor_rectangle = SetCursorRectangle,
    .set_preferred_language = SetPreferredLanguage,
    .update_state = UpdateState,
};

TextInputV2::TextInputV2(wl_resource* resource, wlr_seat_client* seat_client)
    : resource_(resource), seat_(seat_client->seat) {
  wl_resource_set_implementation(resource_, &kImplementation, this,
                                 OnResourceDestroy);
  seat_destroy_.Connect(&seat_client->events.destroy);
}

TextInputV2::~TextInputV2() { MakeInert(); }

wl_client* TextInputV2::Client() const {
  return resource_ == nullptr ? nullptr : wl_resource_get_client(resource_);
}

wlr_seat* TextInputV2::Seat() const { return seat_; }

wlr_surface* TextInputV2::FocusedSurface() const { return surface_; }

bool TextInputV2::MatchesSurface(wlr_surface* surface) const {
  return surface_ != nullptr && surface_ == surface;
}

bool TextInputV2::Enabled() const { return enabled_; }

TextInput::State TextInputV2::CurrentState() const {
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

void TextInputV2::SendEnter(wlr_surface* surface) {
  if (resource_ == nullptr || surface == nullptr) {
    return;
  }
  zwp_text_input_v2_send_enter(resource_, serial_, surface->resource);
}

void TextInputV2::SendLeave() {
  if (resource_ == nullptr || surface_ == nullptr) {
    return;
  }
  zwp_text_input_v2_send_leave(resource_, serial_, surface_->resource);
  surface_destroy_.Disconnect();
  surface_ = nullptr;
}

void TextInputV2::SendPreedit(std::string_view text, int32_t cursor_begin,
                              int32_t) {
  if (resource_ == nullptr) {
    return;
  }
  const std::string value(text);
  zwp_text_input_v2_send_preedit_cursor(resource_, cursor_begin);
  zwp_text_input_v2_send_preedit_styling(
      resource_, 0, static_cast<uint32_t>(value.size()),
      ZWP_TEXT_INPUT_V2_PREEDIT_STYLE_DEFAULT);
  zwp_text_input_v2_send_preedit_string(resource_, value.c_str(), "");
}

void TextInputV2::SendCommit(std::string_view text) {
  if (resource_ == nullptr) {
    return;
  }
  const std::string value(text);
  zwp_text_input_v2_send_commit_string(resource_, value.c_str());
}

void TextInputV2::SendDelete(uint32_t before_length, uint32_t after_length) {
  if (resource_ == nullptr) {
    return;
  }
  const uint32_t size = static_cast<uint32_t>(surrounding_text_.size());
  const uint32_t index = size > before_length ? size - before_length : 0;
  zwp_text_input_v2_send_delete_surrounding_text(resource_, index,
                                                 before_length + after_length);
}

void TextInputV2::SendDone() {}

TextInputV2* TextInputV2::FromResource(wl_resource* resource) {
  return static_cast<TextInputV2*>(wl_resource_get_user_data(resource));
}

void TextInputV2::OnResourceDestroy(wl_resource* resource) {
  TextInputV2* input = FromResource(resource);
  if (input == nullptr) {
    return;
  }
  input->resource_ = nullptr;
  input->NotifyDestroy();
}

void TextInputV2::OnSeatDestroy(TextInputV2* input, void*) {
  input->MakeInert();
  input->NotifyDestroy();
}

void TextInputV2::OnSurfaceDestroy(TextInputV2* input, void*) {
  input->surface_destroy_.Disconnect();
  input->surface_ = nullptr;
}

void TextInputV2::Destroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void TextInputV2::Enable(wl_client* client, wl_resource* resource,
                         wl_resource* surface) {
  TextInputV2* input = FromResource(resource);
  if (input == nullptr || surface == nullptr ||
      wl_resource_get_client(surface) != client) {
    return;
  }

  input->surface_destroy_.Disconnect();
  input->surface_ = wlr_surface_from_resource(surface);
  input->surface_destroy_.Connect(&input->surface_->events.destroy);
  input->enabled_ = true;
  input->NotifyEnable();
}

void TextInputV2::Disable(wl_client*, wl_resource* resource,
                          wl_resource* surface) {
  TextInputV2* input = FromResource(resource);
  if (input == nullptr || input->surface_ == nullptr ||
      input->surface_->resource != surface) {
    return;
  }

  input->enabled_ = false;
  input->NotifyDisable();
  input->surface_destroy_.Disconnect();
  input->surface_ = nullptr;
}

void TextInputV2::ShowInputPanel(wl_client*, wl_resource*) {}

void TextInputV2::HideInputPanel(wl_client*, wl_resource*) {}

void TextInputV2::SetSurroundingText(wl_client*, wl_resource* resource,
                                     const char* text, int32_t cursor,
                                     int32_t anchor) {
  TextInputV2* input = FromResource(resource);
  if (input == nullptr) {
    return;
  }
  input->surrounding_text_ = text == nullptr ? "" : text;
  input->surrounding_cursor_ = cursor < 0 ? 0 : cursor;
  input->surrounding_anchor_ = anchor < 0 ? 0 : anchor;
  input->surrounding_pending_ = true;
}

void TextInputV2::SetContentType(wl_client*, wl_resource* resource,
                                 uint32_t hint, uint32_t purpose) {
  TextInputV2* input = FromResource(resource);
  if (input == nullptr) {
    return;
  }
  input->content_hint_ = hint;
  input->content_purpose_ = purpose > ZWP_TEXT_INPUT_V2_CONTENT_PURPOSE_PASSWORD
                                ? purpose + 1
                                : purpose;
  input->content_type_pending_ = true;
}

void TextInputV2::SetCursorRectangle(wl_client*, wl_resource* resource,
                                     int32_t x, int32_t y, int32_t width,
                                     int32_t height) {
  TextInputV2* input = FromResource(resource);
  if (input == nullptr) {
    return;
  }
  input->cursor_rectangle_ = {x, y, width, height};
  input->cursor_rectangle_pending_ = true;
}

void TextInputV2::SetPreferredLanguage(wl_client*, wl_resource*, const char*) {}

void TextInputV2::UpdateState(wl_client*, wl_resource* resource,
                              uint32_t serial, uint32_t) {
  TextInputV2* input = FromResource(resource);
  if (input == nullptr) {
    return;
  }
  input->serial_ = serial;
  input->NotifyCommit();
  input->surrounding_pending_ = false;
  input->content_type_pending_ = false;
}

void TextInputV2::MakeInert() {
  seat_destroy_.Disconnect();
  surface_destroy_.Disconnect();
  seat_ = nullptr;
  surface_ = nullptr;
  enabled_ = false;
  if (resource_ != nullptr) {
    wl_resource_set_user_data(resource_, nullptr);
    resource_ = nullptr;
  }
}

}  // namespace protocol
}  // namespace flakewm
