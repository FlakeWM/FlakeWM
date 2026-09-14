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

#include <absl/log/absl_log.h>

#include <memory>

#include "protocol/ukui-blur-v1-protocol.h"
#include "protocol/ukui-output-v1-protocol.h"
#include "protocol/ukui-shell-protocol.h"
#include "protocol/ukui-window-management-protocol.h"
#include "src/core/compositor_private/compositor_private.h"
#include "src/protocol/ukui/ukui_protocol_manager_internal.h"
#include "src/protocol/ukui/ukui_protocol_manager.h"
#include "src/render/backdrop_blur_renderer.h"

namespace flakewm {
namespace protocol {
namespace {
using ukui_internal::kBlurVersion;
using ukui_internal::kOutputVersion;
using ukui_internal::kShellVersion;
using ukui_internal::kWindowVersion;
}  // namespace

UkuiProtocolManager::Impl::Impl(core::CompositorPrivate* compositor)
    : compositor(compositor) {}

UkuiProtocolManager::Impl::~Impl() {
  for (wl_global* global :
       {blur_global, output_global, shell_global, window_global}) {
    if (global != nullptr) {
      wl_global_destroy(global);
    }
  }
  while (!blurs.empty()) {
    wl_resource_destroy(blurs.back()->resource);
  }
  while (!shell_surfaces.empty()) {
    wl_resource_destroy(shell_surfaces.back()->resource);
  }
  while (!outputs.empty()) {
    RemoveOutput(outputs.back().get());
  }
}

bool UkuiProtocolManager::Impl::Create(wl_display* new_display,
                                       wlr_seat* new_seat,
                                       wlr_output_layout* new_output_layout) {
  display = new_display;
  seat = new_seat;
  output_layout = new_output_layout;
  output_global =
      wl_global_create(display, &ukui_output_management_v1_interface,
                       kOutputVersion, this, BindOutputManagement);
  shell_global = wl_global_create(display, &ukui_shell_interface, kShellVersion,
                                  this, BindShell);
  window_global = wl_global_create(display, &ukui_window_management_interface,
                                   kWindowVersion, this, BindWindowManagement);
  const bool blur_supported =
      compositor->backdrop_blur_renderer_ != nullptr &&
      compositor->backdrop_blur_renderer_->IsSupported();
  if (blur_supported) {
    blur_global = wl_global_create(display, &ukui_blur_manager_v1_interface,
                                   kBlurVersion, this, BindBlurManager);
  } else {
    ABSL_LOG(WARNING) << "UKUI blur is unavailable on the selected renderer";
  }
  return output_global != nullptr && shell_global != nullptr &&
         window_global != nullptr &&
         (!blur_supported || blur_global != nullptr);
}

void UkuiProtocolManager::Impl::DestroyResourceRequest(wl_client*,
                                                       wl_resource* resource) {
  wl_resource_destroy(resource);
}

UkuiProtocolManager::UkuiProtocolManager(core::CompositorPrivate* compositor)
    : impl_(std::make_unique<Impl>(compositor)) {}

UkuiProtocolManager::~UkuiProtocolManager() = default;

bool UkuiProtocolManager::Create(wl_display* display, wlr_seat* seat,
                                 wlr_output_layout* output_layout) {
  return impl_->Create(display, seat, output_layout);
}

void UkuiProtocolManager::AddOutput(wlr_output* output) {
  impl_->AddOutput(output);
}

void UkuiProtocolManager::UpdateOutputs() { impl_->UpdateOutputs(); }

void UkuiProtocolManager::MapToplevel(wlr_surface* surface) {
  impl_->MapToplevel(surface);
}

void UkuiProtocolManager::UnmapToplevel(wlr_surface* surface) {
  impl_->UnmapToplevel(surface);
}

void UkuiProtocolManager::UpdateToplevel(wlr_surface* surface) {
  impl_->UpdateToplevel(surface);
}

void UkuiProtocolManager::UpdateToplevelParent(wlr_surface* surface,
                                               wlr_surface* parent) {
  impl_->UpdateToplevelParent(surface, parent);
}

}  // namespace protocol
}  // namespace flakewm
