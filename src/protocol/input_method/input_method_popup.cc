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

#include "src/protocol/input_method/input_method_popup.h"
#include "src/protocol/input_method/input_method_relay.h"

namespace flakewm {
namespace protocol {

InputMethodPopup::InputMethodPopup(InputMethodRelay* relay,
                                   wlr_input_popup_surface_v2* handle,
                                   wlr_scene_tree* parent)
    : relay_(relay), handle_(handle) {
  scene_tree_ = wlr_scene_subsurface_tree_create(parent, handle_->surface);
  if (scene_tree_ == nullptr) {
    return;
  }
  wlr_scene_node_set_enabled(&scene_tree_->node, handle_->surface->mapped);
  map_.Connect(&handle_->surface->events.map);
  unmap_.Connect(&handle_->surface->events.unmap);
  commit_.Connect(&handle_->surface->events.commit);
  destroy_.Connect(&handle_->events.destroy);
}

InputMethodPopup::~InputMethodPopup() {
  if (scene_tree_ != nullptr) {
    wlr_scene_node_destroy(&scene_tree_->node);
  }
}

bool InputMethodPopup::IsValid() const { return scene_tree_ != nullptr; }

wlr_input_popup_surface_v2* InputMethodPopup::Handle() const { return handle_; }

wlr_scene_tree* InputMethodPopup::SceneTree() const { return scene_tree_; }

void InputMethodPopup::OnMap(InputMethodPopup* popup, void*) {
  wlr_scene_node_set_enabled(&popup->scene_tree_->node, true);
  popup->relay_->UpdatePopup(popup);
}

void InputMethodPopup::OnUnmap(InputMethodPopup* popup, void*) {
  wlr_scene_node_set_enabled(&popup->scene_tree_->node, false);
}

void InputMethodPopup::OnCommit(InputMethodPopup* popup, void*) {
  popup->relay_->UpdatePopup(popup);
}

void InputMethodPopup::OnDestroy(InputMethodPopup* popup, void*) {
  popup->map_.Disconnect();
  popup->unmap_.Disconnect();
  popup->commit_.Disconnect();
  popup->destroy_.Disconnect();
  popup->handle_ = nullptr;
  popup->relay_->RemovePopup(popup);
}

}  // namespace protocol
}  // namespace flakewm
