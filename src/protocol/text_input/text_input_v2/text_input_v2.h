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

#ifndef SRC_PROTOCOL_TEXT_INPUT_TEXT_INPUT_V2_TEXT_INPUT_V2_H_
#define SRC_PROTOCOL_TEXT_INPUT_TEXT_INPUT_V2_TEXT_INPUT_V2_H_

#include <cstdint>
#include <string>
#include <string_view>

#include "src/protocol/text_input/text_input/text_input.h"
#include "src/utils/signal_listener.h"

struct zwp_text_input_v2_interface;

namespace flakewm {
namespace protocol {

// Implements one zwp_text_input_v2 resource.
class TextInputV2 final : public TextInput {
 public:
  TextInputV2(wl_resource* resource, wlr_seat_client* seat_client);
  ~TextInputV2() override;

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
  static TextInputV2* FromResource(wl_resource* resource);
  static void OnResourceDestroy(wl_resource* resource);
  static void OnSeatDestroy(TextInputV2* input, void*);
  static void OnSurfaceDestroy(TextInputV2* input, void*);

  static void Destroy(wl_client*, wl_resource* resource);
  static void Enable(wl_client* client, wl_resource* resource,
                     wl_resource* surface);
  static void Disable(wl_client* client, wl_resource* resource,
                      wl_resource* surface);
  static void ShowInputPanel(wl_client*, wl_resource*);
  static void HideInputPanel(wl_client*, wl_resource*);
  static void SetSurroundingText(wl_client*, wl_resource* resource,
                                 const char* text, int32_t cursor,
                                 int32_t anchor);
  static void SetContentType(wl_client*, wl_resource* resource, uint32_t hint,
                             uint32_t purpose);
  static void SetCursorRectangle(wl_client*, wl_resource* resource, int32_t x,
                                 int32_t y, int32_t width, int32_t height);
  static void SetPreferredLanguage(wl_client*, wl_resource*, const char*);
  static void UpdateState(wl_client*, wl_resource* resource, uint32_t serial,
                          uint32_t reason);

  static const struct zwp_text_input_v2_interface kImplementation;

  void MakeInert();

  wl_resource* resource_ = nullptr;
  wlr_seat* seat_ = nullptr;
  wlr_surface* surface_ = nullptr;
  std::string surrounding_text_;
  uint32_t surrounding_cursor_ = 0;
  uint32_t surrounding_anchor_ = 0;
  uint32_t content_hint_ = 0;
  uint32_t content_purpose_ = 0;
  uint32_t serial_ = 0;
  wlr_box cursor_rectangle_ = {};
  bool surrounding_pending_ = false;
  bool content_type_pending_ = false;
  bool cursor_rectangle_pending_ = false;
  bool enabled_ = false;
  utils::SignalListener<TextInputV2, void> seat_destroy_{this, OnSeatDestroy};
  utils::SignalListener<TextInputV2, void> surface_destroy_{this,
                                                            OnSurfaceDestroy};
};

}  // namespace protocol
}  // namespace flakewm

#endif  // SRC_PROTOCOL_TEXT_INPUT_TEXT_INPUT_V2_TEXT_INPUT_V2_H_
