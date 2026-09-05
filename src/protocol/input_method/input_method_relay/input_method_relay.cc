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
#include <string>
#include <utility>

#include "src/protocol/input_method/input_method_popup/input_method_popup.h"
#include "src/protocol/text_input/text_input/text_input.h"
#include "src/protocol/text_input/text_input_v1_manager/text_input_v1_manager.h"
#include "src/protocol/text_input/text_input_v2_manager/text_input_v2_manager.h"
#include "src/protocol/text_input/text_input_v3/text_input_v3.h"
#include "src/protocol/input_method/input_method_relay/input_method_relay.h"

namespace flakewm {
namespace protocol {
namespace {

struct SurfacePosition {
  wlr_surface* surface = nullptr;
  int x = 0;
  int y = 0;
  bool found = false;
};

void FindSurfacePosition(wlr_scene_buffer* buffer, int x, int y, void* data) {
  auto* position = static_cast<SurfacePosition*>(data);
  wlr_scene_surface* scene_surface = wlr_scene_surface_try_from_buffer(buffer);
  if (!position->found && scene_surface != nullptr &&
      scene_surface->surface == position->surface) {
    position->x = x;
    position->y = y;
    position->found = true;
  }
}

}  // namespace

InputMethodRelay::InputMethodRelay(wl_display* display, wlr_seat* seat,
                                   wlr_scene* scene,
                                   wlr_scene_tree* popup_parent,
                                   wlr_output_layout* output_layout)
    : display_(display),
      seat_(seat),
      scene_(scene),
      popup_parent_(popup_parent),
      output_layout_(output_layout) {
  text_input_v3_manager_ = wlr_text_input_manager_v3_create(display_);
  input_method_manager_ = wlr_input_method_manager_v2_create(display_);
  text_input_v1_manager_ = std::make_unique<TextInputV1Manager>(display_, this);
  text_input_v2_manager_ = std::make_unique<TextInputV2Manager>(display_, this);

  if (text_input_v3_manager_ != nullptr) {
    new_text_input_v3_.Connect(&text_input_v3_manager_->events.new_text_input);
  }
  if (input_method_manager_ != nullptr) {
    new_input_method_.Connect(&input_method_manager_->events.new_input_method);
  }
  keyboard_focus_change_.Connect(&seat_->keyboard_state.events.focus_change);
  focused_surface_ = seat_->keyboard_state.focused_surface;
}

InputMethodRelay::~InputMethodRelay() {
  grab_destroy_.Disconnect();
  input_method_destroy_.Disconnect();
  input_method_new_popup_.Disconnect();
  input_method_grab_keyboard_.Disconnect();
  input_method_commit_.Disconnect();
  keyboard_focus_change_.Disconnect();
  new_input_method_.Disconnect();
  new_text_input_v3_.Disconnect();
  popups_.clear();
  text_inputs_.clear();
  text_input_v2_manager_.reset();
  text_input_v1_manager_.reset();
}

bool InputMethodRelay::IsValid() const {
  return text_input_v3_manager_ != nullptr &&
         input_method_manager_ != nullptr &&
         text_input_v1_manager_ != nullptr &&
         text_input_v1_manager_->IsValid() &&
         text_input_v2_manager_ != nullptr && text_input_v2_manager_->IsValid();
}

void InputMethodRelay::AddTextInput(std::unique_ptr<TextInput> input) {
  if (input == nullptr ||
      (input->Seat() != nullptr && input->Seat() != seat_)) {
    return;
  }
  input->AttachRelay(this);
  TextInput* value = input.get();
  text_inputs_.push_back(std::move(input));
  if (focused_surface_ != nullptr && value->MatchesSurface(focused_surface_)) {
    value->SendEnter(focused_surface_);
    value->SetRelayFocusedSurface(focused_surface_);
  }
}

void InputMethodRelay::HandleEnable(TextInput* input) {
  UpdateFocus(focused_surface_);
  if (input->RelayFocusedSurface() != focused_surface_) {
    return;
  }
  Activate(input);
}

void InputMethodRelay::HandleCommit(TextInput* input) {
  if (input != active_text_input_ || !input->Enabled()) {
    return;
  }
  if (input_method_ != nullptr) {
    SendState(input);
  }
  UpdatePopups();
}

void InputMethodRelay::HandleDisable(TextInput* input) {
  if (pending_text_input_ == input) {
    pending_text_input_ = nullptr;
  }
  if (active_text_input_ == input) {
    Deactivate(input);
  }
}

void InputMethodRelay::HandleDestroy(TextInput* input) {
  if (pending_text_input_ == input) {
    pending_text_input_ = nullptr;
  }
  if (active_text_input_ == input) {
    Deactivate(input);
  }
  std::erase_if(text_inputs_, [input](const std::unique_ptr<TextInput>& value) {
    return value.get() == input;
  });
}

wlr_input_method_keyboard_grab_v2* InputMethodRelay::GrabForKeyboard(
    wlr_keyboard* keyboard) const {
  if (input_method_ == nullptr || input_method_->keyboard_grab == nullptr ||
      keyboard == nullptr) {
    return nullptr;
  }
  wlr_virtual_keyboard_v1* virtual_keyboard =
      wlr_input_device_get_virtual_keyboard(&keyboard->base);
  if (virtual_keyboard != nullptr &&
      wl_resource_get_client(virtual_keyboard->resource) ==
          wl_resource_get_client(input_method_->resource)) {
    return nullptr;
  }
  return input_method_->keyboard_grab;
}

void InputMethodRelay::OnNewTextInputV3(InputMethodRelay* relay,
                                        wlr_text_input_v3* input) {
  if (input->seat == relay->seat_) {
    relay->AddTextInput(std::make_unique<TextInputV3>(input));
  }
}

void InputMethodRelay::OnNewInputMethod(InputMethodRelay* relay,
                                        wlr_input_method_v2* input_method) {
  if (input_method->seat != relay->seat_) {
    return;
  }
  if (relay->input_method_ != nullptr) {
    wlr_input_method_v2_send_unavailable(input_method);
    return;
  }

  relay->input_method_ = input_method;
  relay->input_method_commit_.Connect(&input_method->events.commit);
  relay->input_method_grab_keyboard_.Connect(
      &input_method->events.grab_keyboard);
  relay->input_method_new_popup_.Connect(
      &input_method->events.new_popup_surface);
  relay->input_method_destroy_.Connect(&input_method->events.destroy);

  relay->UpdateFocus(relay->focused_surface_);
  TextInput* candidate = relay->pending_text_input_;
  if (candidate == nullptr) {
    for (const std::unique_ptr<TextInput>& input : relay->text_inputs_) {
      if (input->Enabled() &&
          input->RelayFocusedSurface() == relay->focused_surface_) {
        candidate = input.get();
        break;
      }
    }
  }
  relay->pending_text_input_ = nullptr;
  if (candidate != nullptr) {
    relay->Activate(candidate);
  }
}

void InputMethodRelay::OnKeyboardFocusChange(
    InputMethodRelay* relay, wlr_seat_keyboard_focus_change_event* event) {
  relay->UpdateFocus(event->new_surface);
}

void InputMethodRelay::OnInputMethodCommit(InputMethodRelay* relay, void*) {
  if (relay->input_method_ == nullptr || relay->active_text_input_ == nullptr) {
    return;
  }

  const wlr_input_method_v2_state& state = relay->input_method_->current;
  if (state.preedit.text != nullptr) {
    relay->active_text_input_->SendPreedit(state.preedit.text,
                                           state.preedit.cursor_begin,
                                           state.preedit.cursor_end);
  }
  if (state.commit_text != nullptr) {
    relay->active_text_input_->SendCommit(state.commit_text);
  }
  if (state.delete_.before_length != 0 || state.delete_.after_length != 0) {
    relay->active_text_input_->SendDelete(state.delete_.before_length,
                                          state.delete_.after_length);
  }
  relay->active_text_input_->SendDone();
}

void InputMethodRelay::OnInputMethodGrabKeyboard(
    InputMethodRelay* relay, wlr_input_method_keyboard_grab_v2* grab) {
  relay->grab_destroy_.Disconnect();
  if (wlr_keyboard* keyboard = wlr_seat_get_keyboard(relay->seat_);
      keyboard != nullptr) {
    wlr_input_method_keyboard_grab_v2_set_keyboard(grab, keyboard);
    wlr_input_method_keyboard_grab_v2_send_modifiers(grab,
                                                     &keyboard->modifiers);
  }
  relay->grab_destroy_.Connect(&grab->events.destroy);
}

void InputMethodRelay::OnInputMethodNewPopup(
    InputMethodRelay* relay, wlr_input_popup_surface_v2* popup) {
  auto value =
      std::make_unique<InputMethodPopup>(relay, popup, relay->popup_parent_);
  if (!value->IsValid()) {
    return;
  }
  InputMethodPopup* pointer = value.get();
  relay->popups_.push_back(std::move(value));
  relay->UpdatePopup(pointer);
}

void InputMethodRelay::OnInputMethodDestroy(InputMethodRelay* relay, void*) {
  relay->grab_destroy_.Disconnect();
  relay->input_method_commit_.Disconnect();
  relay->input_method_grab_keyboard_.Disconnect();
  relay->input_method_new_popup_.Disconnect();
  relay->input_method_destroy_.Disconnect();
  relay->input_method_ = nullptr;
  relay->pending_text_input_ = relay->active_text_input_;
  relay->active_text_input_ = nullptr;
  relay->popups_.clear();
}

void InputMethodRelay::OnGrabDestroy(InputMethodRelay* relay, void*) {
  relay->grab_destroy_.Disconnect();
}

bool InputMethodRelay::AcceptsFocus(const TextInput* input,
                                    wlr_surface* surface) const {
  return surface != nullptr &&
         (input->Seat() == nullptr || input->Seat() == seat_) &&
         input->MatchesSurface(surface);
}

void InputMethodRelay::UpdateFocus(wlr_surface* surface) {
  focused_surface_ = surface;
  for (const std::unique_ptr<TextInput>& input : text_inputs_) {
    if (input->RelayFocusedSurface() != nullptr &&
        input->RelayFocusedSurface() != surface) {
      if (active_text_input_ == input.get()) {
        Deactivate(input.get());
      }
      input->SendLeave();
      input->SetRelayFocusedSurface(nullptr);
    }
  }

  TextInput* candidate = nullptr;
  for (const std::unique_ptr<TextInput>& input : text_inputs_) {
    if (input->RelayFocusedSurface() == nullptr &&
        AcceptsFocus(input.get(), surface)) {
      input->SendEnter(surface);
      input->SetRelayFocusedSurface(surface);
    }
    if (candidate == nullptr && input->Enabled() &&
        input->RelayFocusedSurface() == surface) {
      candidate = input.get();
    }
  }
  if (surface != nullptr && candidate != nullptr &&
      candidate != active_text_input_) {
    Activate(candidate);
  }
  UpdatePopups();
}

void InputMethodRelay::Activate(TextInput* input) {
  if (input == nullptr || focused_surface_ == nullptr || !input->Enabled() ||
      input->RelayFocusedSurface() != focused_surface_) {
    return;
  }
  if (input_method_ == nullptr) {
    pending_text_input_ = input;
    return;
  }
  if (active_text_input_ != nullptr && active_text_input_ != input) {
    Deactivate(active_text_input_);
  }
  active_text_input_ = input;
  pending_text_input_ = nullptr;
  wlr_input_method_v2_send_activate(input_method_);
  SendState(input);
}

void InputMethodRelay::Deactivate(TextInput* input) {
  if (input == nullptr) {
    return;
  }
  if (input_method_ != nullptr && active_text_input_ == input) {
    wlr_input_method_v2_send_deactivate(input_method_);
    SendState(input);
  }
  if (active_text_input_ == input) {
    active_text_input_ = nullptr;
  }
}

void InputMethodRelay::SendState(TextInput* input) {
  if (input_method_ == nullptr || input == nullptr) {
    return;
  }
  const TextInput::State state = input->CurrentState();
  if (state.has_surrounding_text) {
    const std::string surrounding_text(state.surrounding_text);
    wlr_input_method_v2_send_surrounding_text(
        input_method_, surrounding_text.c_str(), state.surrounding_cursor,
        state.surrounding_anchor);
  }
  wlr_input_method_v2_send_text_change_cause(input_method_,
                                             state.text_change_cause);
  if (state.has_content_type) {
    wlr_input_method_v2_send_content_type(input_method_, state.content_hint,
                                          state.content_purpose);
  }
  wlr_input_method_v2_send_done(input_method_);
  UpdatePopups();
}

void InputMethodRelay::UpdatePopup(InputMethodPopup* popup) {
  if (popup == nullptr || active_text_input_ == nullptr ||
      focused_surface_ == nullptr || popup->Handle() == nullptr ||
      !popup->Handle()->surface->mapped) {
    return;
  }

  SurfacePosition position{.surface = focused_surface_};
  wlr_scene_node_for_each_buffer(&scene_->tree.node, FindSurfacePosition,
                                 &position);
  if (!position.found) {
    return;
  }

  const TextInput::State state = active_text_input_->CurrentState();
  wlr_box cursor =
      state.has_cursor_rectangle ? state.cursor_rectangle : wlr_box{};
  cursor.x += position.x;
  cursor.y += position.y;

  wlr_output* output =
      wlr_output_layout_output_at(output_layout_, cursor.x, cursor.y);
  wlr_box output_box = {};
  wlr_output_layout_get_box(output_layout_, output, &output_box);
  if (output_box.width <= 0 || output_box.height <= 0) {
    return;
  }

  const int popup_width = popup->Handle()->surface->current.width;
  const int popup_height = popup->Handle()->surface->current.height;
  int popup_x = cursor.x;
  int popup_y = cursor.y + cursor.height;
  if (popup_x + popup_width > output_box.x + output_box.width) {
    popup_x = output_box.x + output_box.width - popup_width;
  }
  if (popup_x < output_box.x) {
    popup_x = output_box.x;
  }
  if (popup_y + popup_height > output_box.y + output_box.height) {
    popup_y = cursor.y - popup_height;
  }
  if (popup_y < output_box.y) {
    popup_y = output_box.y;
  }
  wlr_scene_node_set_position(&popup->SceneTree()->node, popup_x, popup_y);

  wlr_box popup_cursor = {
      .x = cursor.x - popup_x,
      .y = cursor.y - popup_y,
      .width = cursor.width,
      .height = cursor.height,
  };
  wlr_input_popup_surface_v2_send_text_input_rectangle(popup->Handle(),
                                                       &popup_cursor);
}

void InputMethodRelay::RemovePopup(InputMethodPopup* popup) {
  std::erase_if(popups_,
                [popup](const std::unique_ptr<InputMethodPopup>& value) {
                  return value.get() == popup;
                });
}

void InputMethodRelay::UpdatePopups() {
  for (const std::unique_ptr<InputMethodPopup>& popup : popups_) {
    UpdatePopup(popup.get());
  }
}

}  // namespace protocol
}  // namespace flakewm
