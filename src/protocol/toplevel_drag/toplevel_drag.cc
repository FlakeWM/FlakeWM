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

#include "src/protocol/toplevel_drag/toplevel_drag.h"
#include "src/protocol/toplevel_drag/toplevel_drag_manager.h"

namespace flakewm {
namespace protocol {

const struct xdg_toplevel_drag_v1_interface ToplevelDrag::kImplementation = {
    .destroy = Destroy,
    .attach = Attach,
};

ToplevelDrag::ToplevelDrag(ToplevelDragManager* manager, wl_resource* resource,
                           wlr_data_source* source)
    : manager_(manager), resource_(resource), source_(source) {
  source_destroy_.Connect(&source_->events.destroy);
  wl_resource_set_implementation(resource_, &kImplementation, this,
                                 OnResourceDestroyed);
}

ToplevelDrag::~ToplevelDrag() {
  source_destroy_.Disconnect();
  toplevel_unmap_.Disconnect();
  toplevel_destroy_.Disconnect();
}

wlr_data_source* ToplevelDrag::Source() const { return source_; }

wlr_xdg_toplevel* ToplevelDrag::Toplevel() const { return toplevel_; }

int ToplevelDrag::OffsetX() const { return offset_x_; }

int ToplevelDrag::OffsetY() const { return offset_y_; }

void ToplevelDrag::MakeInert() {
  source_destroy_.Disconnect();
  toplevel_unmap_.Disconnect();
  toplevel_destroy_.Disconnect();
  source_ = nullptr;
  toplevel_ = nullptr;
}

void ToplevelDrag::Destroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void ToplevelDrag::Attach(wl_client*, wl_resource* resource,
                          wl_resource* toplevel, int32_t x_offset,
                          int32_t y_offset) {
  auto* drag = static_cast<ToplevelDrag*>(wl_resource_get_user_data(resource));
  if (drag == nullptr) {
    return;
  }
  if (drag->toplevel_ != nullptr) {
    wl_resource_post_error(resource,
                           XDG_TOPLEVEL_DRAG_V1_ERROR_TOPLEVEL_ATTACHED,
                           "a toplevel is already attached");
    return;
  }

  drag->toplevel_ = wlr_xdg_toplevel_from_resource(toplevel);
  if (drag->toplevel_ == nullptr) {
    return;
  }
  drag->offset_x_ = x_offset;
  drag->offset_y_ = y_offset;
  drag->toplevel_unmap_.Connect(&drag->toplevel_->base->surface->events.unmap);
  drag->toplevel_destroy_.Connect(&drag->toplevel_->base->events.destroy);
}

void ToplevelDrag::OnResourceDestroyed(wl_resource* resource) {
  auto* drag = static_cast<ToplevelDrag*>(wl_resource_get_user_data(resource));
  if (drag != nullptr) {
    drag->resource_ = nullptr;
    drag->manager_->Remove(drag);
  }
}

void ToplevelDrag::OnSourceDestroyed(ToplevelDrag* drag, void*) {
  if (drag->resource_ != nullptr) {
    wl_resource_set_user_data(drag->resource_, nullptr);
  }
  drag->manager_->Remove(drag);
}

void ToplevelDrag::OnToplevelGone(ToplevelDrag* drag, void*) {
  drag->toplevel_unmap_.Disconnect();
  drag->toplevel_destroy_.Disconnect();
  drag->toplevel_ = nullptr;
}

}  // namespace protocol
}  // namespace flakewm
