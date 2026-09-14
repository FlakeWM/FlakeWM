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
 * This is an implementation of GXDE Protocol manager.
 */

#include <cstring>
#include <memory>

#include "src/core/compositor_private/compositor_private.h"
#include "src/protocol/gxde/gxde_protocol_manager_internal.h"
#include "src/protocol/protocol_manager/protocol_manager.h"
#include "src/protocol/gxde/gxde_protocol_manager.h"

namespace flakewm {
namespace protocol {

GxdeProtocolManager::GxdeProtocolManager(core::CompositorPrivate* compositor,
                                         ProtocolManager* protocol_manager)
    : compositor_(compositor), protocol_manager_(protocol_manager) {}

GxdeProtocolManager::~GxdeProtocolManager() {
  screenshot_manager_.reset();
  if (identifier_global_ != nullptr) {
    wl_global_destroy(identifier_global_);
  }
}

bool GxdeProtocolManager::Create(wl_display* display) {
  identifier_global_ = CreateGxdeIdentifierGlobal(display);
  screenshot_manager_ = std::make_unique<GxdeScreenshotManager>(this, display);
  return identifier_global_ != nullptr && screenshot_manager_->IsValid();
}

wlr_output* GxdeProtocolManager::FindOutput(const char* identifier) const {
  if (identifier == nullptr || identifier[0] == '\0' ||
      compositor_ == nullptr) {
    return nullptr;
  }
  for (const std::unique_ptr<core::CompositorPrivate::Output>& output :
       compositor_->outputs_) {
    if (output->handle != nullptr && output->handle->enabled &&
        output->handle->name != nullptr &&
        std::strcmp(output->handle->name, identifier) == 0) {
      return output->handle;
    }
  }
  return nullptr;
}

GxdeProtocolManager::WindowTarget GxdeProtocolManager::FindWindow(
    const char* identifier, const char* app_id, bool without_decoration) const {
  WindowTarget result;
  if (identifier == nullptr || identifier[0] == '\0' ||
      compositor_ == nullptr || protocol_manager_ == nullptr) {
    return result;
  }
  for (const std::unique_ptr<ForeignToplevel>& foreign :
       protocol_manager_->foreign_toplevels_) {
    wlr_ext_foreign_toplevel_handle_v1* ext = foreign->ExtHandle();
    if (ext == nullptr || ext->identifier == nullptr ||
        std::strcmp(ext->identifier, identifier) != 0 ||
        (app_id != nullptr && app_id[0] != '\0' &&
         (ext->app_id == nullptr || std::strcmp(ext->app_id, app_id) != 0))) {
      continue;
    }
    core::CompositorPrivate::Toplevel* toplevel =
        compositor_->ToplevelForSurface(foreign->Surface());
    if (toplevel == nullptr || !toplevel->mapped ||
        toplevel->scene_tree == nullptr || toplevel->Surface() == nullptr) {
      return result;
    }
    result.surface = toplevel->Surface();
    result.scene_node = &toplevel->scene_tree->node;
    if (!without_decoration) {
      result.scene_source = foreign->CaptureSource(
          result.scene_node, wl_display_get_event_loop(compositor_->display_),
          compositor_->allocator_, compositor_->renderer_);
    }
    return result;
  }
  return result;
}

bool GxdeProtocolManager::CaptureAllowed() const {
  return protocol_manager_ != nullptr && !protocol_manager_->SessionLocked();
}

wl_event_loop* GxdeProtocolManager::EventLoop() const {
  return compositor_ == nullptr || compositor_->display_ == nullptr
             ? nullptr
             : wl_display_get_event_loop(compositor_->display_);
}

wlr_renderer* GxdeProtocolManager::Renderer() const {
  return compositor_ == nullptr ? nullptr : compositor_->renderer_;
}

}  // namespace protocol
}  // namespace flakewm
