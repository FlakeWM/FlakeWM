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
 * Originally copyright by (C) 2024 KylinSoft Co., Ltd.
 * Original license: GPL-1.0-or-later, see Open Kylin Wayland Compositor.
 * Redistributed with GPL-3.0-or-later.
 * Original code is modified to adapt C++ and Wlroots 0.20.2.
 */

#include <new>

#include "src/protocol/toplevel_drag/toplevel_drag_manager.h"

namespace flakewm {
namespace protocol {

const struct xdg_toplevel_drag_manager_v1_interface
    ToplevelDragManager::kImplementation = {
        .destroy = Destroy,
        .get_xdg_toplevel_drag = GetToplevelDrag,
};

ToplevelDragManager::ToplevelDragManager(wl_display* display) {
  global_ = wl_global_create(display, &xdg_toplevel_drag_manager_v1_interface,
                             1, this, Bind);
}

ToplevelDragManager::~ToplevelDragManager() {
  for (const std::unique_ptr<ToplevelDrag>& drag : drags_) {
    drag->MakeInert();
  }
  drags_.clear();
  if (global_ != nullptr) {
    wl_global_destroy(global_);
  }
}

bool ToplevelDragManager::IsValid() const { return global_ != nullptr; }

ToplevelDrag* ToplevelDragManager::Find(wlr_data_source* source) const {
  for (const std::unique_ptr<ToplevelDrag>& drag : drags_) {
    if (drag->Source() == source) {
      return drag.get();
    }
  }
  return nullptr;
}

void ToplevelDragManager::Remove(ToplevelDrag* drag) {
  for (auto iterator = drags_.begin(); iterator != drags_.end(); ++iterator) {
    if (iterator->get() == drag) {
      drags_.erase(iterator);
      return;
    }
  }
}

void ToplevelDragManager::Bind(wl_client* client, void* data, uint32_t version,
                               uint32_t id) {
  wl_resource* resource = wl_resource_create(
      client, &xdg_toplevel_drag_manager_v1_interface, version, id);
  if (resource == nullptr) {
    wl_client_post_no_memory(client);
    return;
  }
  wl_resource_set_implementation(resource, &kImplementation, data, nullptr);
}

void ToplevelDragManager::Destroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void ToplevelDragManager::GetToplevelDrag(wl_client* client,
                                          wl_resource* resource, uint32_t id,
                                          wl_resource* data_source) {
  auto* manager =
      static_cast<ToplevelDragManager*>(wl_resource_get_user_data(resource));
  auto* source =
      static_cast<wlr_data_source*>(wl_resource_get_user_data(data_source));
  if (source == nullptr || manager->Find(source) != nullptr) {
    wl_resource_post_error(
        resource, XDG_TOPLEVEL_DRAG_MANAGER_V1_ERROR_INVALID_SOURCE,
        "data source is invalid or already used for a toplevel drag");
    return;
  }

  wl_resource* drag_resource =
      wl_resource_create(client, &xdg_toplevel_drag_v1_interface, 1, id);
  if (drag_resource == nullptr) {
    wl_client_post_no_memory(client);
    return;
  }

  auto drag = std::unique_ptr<ToplevelDrag>(
      new (std::nothrow) ToplevelDrag(manager, drag_resource, source));
  if (drag == nullptr) {
    wl_resource_destroy(drag_resource);
    wl_client_post_no_memory(client);
    return;
  }
  manager->drags_.push_back(std::move(drag));
}

}  // namespace protocol
}  // namespace flakewm
