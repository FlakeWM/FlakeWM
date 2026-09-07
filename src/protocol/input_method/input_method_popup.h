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

#ifndef SRC_PROTOCOL_INPUT_METHOD_INPUT_METHOD_POPUP_INPUT_METHOD_POPUP_H_
#define SRC_PROTOCOL_INPUT_METHOD_INPUT_METHOD_POPUP_INPUT_METHOD_POPUP_H_

#include "src/utils/signal_listener.h"
#include "src/wlr_wrapper/wlroots.h"

namespace flakewm {
namespace protocol {

class InputMethodRelay;

// Owns the scene subtree for one input-method candidate popup.
class InputMethodPopup final {
 public:
  InputMethodPopup(InputMethodRelay* relay, wlr_input_popup_surface_v2* handle,
                   wlr_scene_tree* parent);
  ~InputMethodPopup();

  InputMethodPopup(const InputMethodPopup&) = delete;
  InputMethodPopup& operator=(const InputMethodPopup&) = delete;

  bool IsValid() const;
  wlr_input_popup_surface_v2* Handle() const;
  wlr_scene_tree* SceneTree() const;

 private:
  static void OnMap(InputMethodPopup* popup, void*);
  static void OnUnmap(InputMethodPopup* popup, void*);
  static void OnCommit(InputMethodPopup* popup, void*);
  static void OnDestroy(InputMethodPopup* popup, void*);

  InputMethodRelay* relay_ = nullptr;
  wlr_input_popup_surface_v2* handle_ = nullptr;
  wlr_scene_tree* scene_tree_ = nullptr;
  utils::SignalListener<InputMethodPopup, void> map_{this, OnMap};
  utils::SignalListener<InputMethodPopup, void> unmap_{this, OnUnmap};
  utils::SignalListener<InputMethodPopup, void> commit_{this, OnCommit};
  utils::SignalListener<InputMethodPopup, void> destroy_{this, OnDestroy};
};

}  // namespace protocol
}  // namespace flakewm

#endif  // SRC_PROTOCOL_INPUT_METHOD_INPUT_METHOD_POPUP_INPUT_METHOD_POPUP_H_
