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

#include "src/protocol/foreign_toplevel/foreign_toplevel.h"
#include "src/protocol/protocol_manager/protocol_manager.h"

namespace flakewm {
namespace protocol {

ForeignToplevel::ForeignToplevel(
    ProtocolManager* manager, wlr_foreign_toplevel_manager_v1* foreign_manager,
    wlr_surface* surface)
    : manager_(manager),
      surface_(surface),
      handle_(wlr_foreign_toplevel_handle_v1_create(foreign_manager)) {
  if (handle_ == nullptr) {
    return;
  }
  request_maximize_.Connect(&handle_->events.request_maximize);
  request_minimize_.Connect(&handle_->events.request_minimize);
  request_activate_.Connect(&handle_->events.request_activate);
  request_fullscreen_.Connect(&handle_->events.request_fullscreen);
  request_close_.Connect(&handle_->events.request_close);
  destroy_.Connect(&handle_->events.destroy);
}

ForeignToplevel::~ForeignToplevel() {
  output_destroy_.Disconnect();
  if (handle_ != nullptr) {
    wlr_foreign_toplevel_handle_v1_destroy(handle_);
  }
}

wlr_surface* ForeignToplevel::Surface() const { return surface_; }

wlr_foreign_toplevel_handle_v1* ForeignToplevel::Handle() const {
  return handle_;
}

bool ForeignToplevel::IsValid() const { return handle_ != nullptr; }

void ForeignToplevel::SetTitle(const char* title) {
  if (handle_ != nullptr) {
    wlr_foreign_toplevel_handle_v1_set_title(handle_,
                                             title == nullptr ? "" : title);
  }
}

void ForeignToplevel::SetAppId(const char* app_id) {
  if (handle_ != nullptr) {
    wlr_foreign_toplevel_handle_v1_set_app_id(handle_,
                                              app_id == nullptr ? "" : app_id);
  }
}

void ForeignToplevel::SetMaximized(bool maximized) {
  if (handle_ != nullptr) {
    wlr_foreign_toplevel_handle_v1_set_maximized(handle_, maximized);
  }
}

void ForeignToplevel::SetMinimized(bool minimized) {
  if (handle_ != nullptr) {
    wlr_foreign_toplevel_handle_v1_set_minimized(handle_, minimized);
  }
}

void ForeignToplevel::SetActivated(bool activated) {
  if (handle_ != nullptr) {
    wlr_foreign_toplevel_handle_v1_set_activated(handle_, activated);
  }
}

void ForeignToplevel::SetFullscreen(bool fullscreen) {
  if (handle_ != nullptr) {
    wlr_foreign_toplevel_handle_v1_set_fullscreen(handle_, fullscreen);
  }
}

void ForeignToplevel::SetOutput(wlr_output* output) {
  if (handle_ == nullptr || output_ == output) {
    return;
  }
  if (output_ != nullptr) {
    output_destroy_.Disconnect();
    wlr_foreign_toplevel_handle_v1_output_leave(handle_, output_);
  }
  output_ = output;
  if (output_ != nullptr) {
    output_destroy_.Connect(&output_->events.destroy);
    wlr_foreign_toplevel_handle_v1_output_enter(handle_, output_);
  }
}

void ForeignToplevel::SetParent(wlr_foreign_toplevel_handle_v1* parent) {
  if (handle_ != nullptr) {
    wlr_foreign_toplevel_handle_v1_set_parent(handle_, parent);
  }
}

void ForeignToplevel::OnRequestMaximize(
    ForeignToplevel* foreign,
    wlr_foreign_toplevel_handle_v1_maximized_event* event) {
  foreign->manager_->RequestMaximize(foreign->surface_, event->maximized);
}

void ForeignToplevel::OnRequestMinimize(
    ForeignToplevel* foreign,
    wlr_foreign_toplevel_handle_v1_minimized_event* event) {
  foreign->manager_->RequestMinimize(foreign->surface_, event->minimized);
}

void ForeignToplevel::OnRequestActivate(
    ForeignToplevel* foreign,
    wlr_foreign_toplevel_handle_v1_activated_event* event) {
  foreign->manager_->RequestActivate(foreign->surface_, event->seat);
}

void ForeignToplevel::OnRequestFullscreen(
    ForeignToplevel* foreign,
    wlr_foreign_toplevel_handle_v1_fullscreen_event* event) {
  foreign->manager_->RequestFullscreen(foreign->surface_, event->fullscreen,
                                       event->output);
}

void ForeignToplevel::OnRequestClose(ForeignToplevel* foreign, void*) {
  foreign->manager_->RequestClose(foreign->surface_);
}

void ForeignToplevel::OnOutputDestroy(ForeignToplevel* foreign, void*) {
  foreign->output_destroy_.Disconnect();
  foreign->output_ = nullptr;
}

void ForeignToplevel::OnDestroy(ForeignToplevel* foreign, void*) {
  foreign->request_maximize_.Disconnect();
  foreign->request_minimize_.Disconnect();
  foreign->request_activate_.Disconnect();
  foreign->request_fullscreen_.Disconnect();
  foreign->request_close_.Disconnect();
  foreign->output_destroy_.Disconnect();
  foreign->destroy_.Disconnect();
  foreign->handle_ = nullptr;
}

}  // namespace protocol
}  // namespace flakewm
