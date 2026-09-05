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

#ifndef SRC_PROTOCOL_INPUT_METHOD_INPUT_METHOD_RELAY_INPUT_METHOD_RELAY_H_
#define SRC_PROTOCOL_INPUT_METHOD_INPUT_METHOD_RELAY_INPUT_METHOD_RELAY_H_

#include <memory>
#include <vector>

#include "src/utils/signal_listener.h"
#include "src/wlr_wrapper/wlroots.h"

namespace flakewm {
namespace protocol {

class InputMethodPopup;
class TextInput;
class TextInputV1Manager;
class TextInputV2Manager;

// Connects all text-input protocol versions to one input-method-v2 client.
class InputMethodRelay final {
 public:
  InputMethodRelay(wl_display* display, wlr_seat* seat, wlr_scene* scene,
                   wlr_scene_tree* popup_parent,
                   wlr_output_layout* output_layout);
  ~InputMethodRelay();

  InputMethodRelay(const InputMethodRelay&) = delete;
  InputMethodRelay& operator=(const InputMethodRelay&) = delete;

  bool IsValid() const;
  void AddTextInput(std::unique_ptr<TextInput> input);
  void HandleEnable(TextInput* input);
  void HandleCommit(TextInput* input);
  void HandleDisable(TextInput* input);
  void HandleDestroy(TextInput* input);
  void UpdatePopup(InputMethodPopup* popup);
  void RemovePopup(InputMethodPopup* popup);

  wlr_input_method_keyboard_grab_v2* GrabForKeyboard(
      wlr_keyboard* keyboard) const;

 private:
  static void OnNewTextInputV3(InputMethodRelay* relay,
                               wlr_text_input_v3* input);
  static void OnNewInputMethod(InputMethodRelay* relay,
                               wlr_input_method_v2* input_method);
  static void OnKeyboardFocusChange(
      InputMethodRelay* relay, wlr_seat_keyboard_focus_change_event* event);
  static void OnInputMethodCommit(InputMethodRelay* relay, void*);
  static void OnInputMethodGrabKeyboard(
      InputMethodRelay* relay, wlr_input_method_keyboard_grab_v2* grab);
  static void OnInputMethodNewPopup(InputMethodRelay* relay,
                                    wlr_input_popup_surface_v2* popup);
  static void OnInputMethodDestroy(InputMethodRelay* relay, void*);
  static void OnGrabDestroy(InputMethodRelay* relay, void*);

  bool AcceptsFocus(const TextInput* input, wlr_surface* surface) const;
  void UpdateFocus(wlr_surface* surface);
  void Activate(TextInput* input);
  void Deactivate(TextInput* input);
  void SendState(TextInput* input);
  void UpdatePopups();

  wl_display* display_ = nullptr;
  wlr_seat* seat_ = nullptr;
  wlr_scene* scene_ = nullptr;
  wlr_scene_tree* popup_parent_ = nullptr;
  wlr_output_layout* output_layout_ = nullptr;
  wlr_text_input_manager_v3* text_input_v3_manager_ = nullptr;
  wlr_input_method_manager_v2* input_method_manager_ = nullptr;
  std::unique_ptr<TextInputV1Manager> text_input_v1_manager_;
  std::unique_ptr<TextInputV2Manager> text_input_v2_manager_;
  wlr_input_method_v2* input_method_ = nullptr;
  wlr_surface* focused_surface_ = nullptr;
  TextInput* active_text_input_ = nullptr;
  TextInput* pending_text_input_ = nullptr;
  std::vector<std::unique_ptr<TextInput>> text_inputs_;
  std::vector<std::unique_ptr<InputMethodPopup>> popups_;

  utils::SignalListener<InputMethodRelay, wlr_text_input_v3> new_text_input_v3_{
      this, OnNewTextInputV3};
  utils::SignalListener<InputMethodRelay, wlr_input_method_v2>
      new_input_method_{this, OnNewInputMethod};
  utils::SignalListener<InputMethodRelay, wlr_seat_keyboard_focus_change_event>
      keyboard_focus_change_{this, OnKeyboardFocusChange};
  utils::SignalListener<InputMethodRelay, void> input_method_commit_{
      this, OnInputMethodCommit};
  utils::SignalListener<InputMethodRelay, wlr_input_method_keyboard_grab_v2>
      input_method_grab_keyboard_{this, OnInputMethodGrabKeyboard};
  utils::SignalListener<InputMethodRelay, wlr_input_popup_surface_v2>
      input_method_new_popup_{this, OnInputMethodNewPopup};
  utils::SignalListener<InputMethodRelay, void> input_method_destroy_{
      this, OnInputMethodDestroy};
  utils::SignalListener<InputMethodRelay, void> grab_destroy_{this,
                                                              OnGrabDestroy};
};

}  // namespace protocol
}  // namespace flakewm

#endif  // SRC_PROTOCOL_INPUT_METHOD_INPUT_METHOD_RELAY_INPUT_METHOD_RELAY_H_
