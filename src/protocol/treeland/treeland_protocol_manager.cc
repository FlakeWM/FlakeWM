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
 * Originally copyright by (C) 2024-2026 UnionTech Software Technology Co., Ltd.
 * Original license: Apache-2.0 OR LGPL-3.0-only OR GPL-2.0-only OR
 *                   GPL-3.0-only.
 * Redistributed with GPL-3.0-only.
 * Original code is modified to adapt C++ and Wlroots 0.20.2.
 */

#include "src/protocol/treeland/treeland_protocol_manager.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

#include "src/core/compositor_private/compositor_private.h"
#include "src/protocol/layer_shell/layer_surface.h"
#include "src/protocol/protocol_manager/protocol_manager.h"
#include "src/protocol/treeland/treeland_protocol_manager_internal.h"
#include "src/render/backdrop_blur_renderer.h"
#include "src/view/window_selecter/window_selector.h"

namespace flakewm {
namespace protocol {

TreelandProtocolManager::TreelandProtocolManager(
    core::CompositorPrivate* compositor, ProtocolManager* protocol_manager)
    : impl_(std::make_unique<TreelandProtocolManagerImpl>(compositor,
                                                          protocol_manager)) {}

TreelandProtocolManager::~TreelandProtocolManager() = default;

bool TreelandProtocolManager::Create(wl_display* display, wlr_seat* seat,
                                     wlr_output_layout* output_layout) {
  return impl_->Create(display, seat, output_layout);
}

bool TreelandProtocolManager::ClientHasWindowContext(
    wlr_surface* surface) const {
  return impl_->ClientHasWindowContext(surface);
}

bool TreelandProtocolManager::IsDarkTheme() const {
  return impl_->IsDarkTheme();
}

TreelandProtocolManagerImpl::TreelandProtocolManagerImpl(
    core::CompositorPrivate* compositor, ProtocolManager* protocol_manager)
    : compositor(compositor), protocol_manager(protocol_manager) {}

TreelandProtocolManagerImpl::~TreelandProtocolManagerImpl() = default;

bool TreelandProtocolManagerImpl::Create(wl_display* new_display,
                                         wlr_seat* new_seat,
                                         wlr_output_layout* new_output_layout) {
  display = new_display;
  seat = new_seat;
  output_layout = new_output_layout;
  globals.push_back(CreateTreelandAppIdResolverGlobal(this, display));
  globals.push_back(CreateTreelandCaptureGlobal(this, display));
  globals.push_back(CreateTreelandDdeShellGlobal(this, display));
  globals.push_back(CreateTreelandPersonalizationGlobal(this, display));
  globals.push_back(CreateLegacyDdeShellGlobal(this, display));
  return std::all_of(globals.begin(), globals.end(), [](const auto& global) {
    return global != nullptr && global->IsValid();
  });
}

wlr_cursor* TreelandProtocolManagerImpl::Cursor() const {
  return compositor == nullptr ? nullptr : compositor->cursor_;
}

wlr_renderer* TreelandProtocolManagerImpl::Renderer() const {
  return compositor == nullptr ? nullptr : compositor->renderer_;
}

wlr_allocator* TreelandProtocolManagerImpl::Allocator() const {
  return compositor == nullptr ? nullptr : compositor->allocator_;
}

wl_event_loop* TreelandProtocolManagerImpl::EventLoop() const {
  return display == nullptr ? nullptr : wl_display_get_event_loop(display);
}

view::WindowSelector* TreelandProtocolManagerImpl::Selector() const {
  return compositor == nullptr ? nullptr : compositor->window_selector_.get();
}

wlr_output* TreelandProtocolManagerImpl::OutputAtCursor() const {
  if (output_layout == nullptr) return nullptr;
  wlr_output* output = nullptr;
  if (Cursor() != nullptr) {
    output =
        wlr_output_layout_output_at(output_layout, Cursor()->x, Cursor()->y);
  }
  return output == nullptr ? wlr_output_layout_get_center_output(output_layout)
                           : output;
}

bool TreelandProtocolManagerImpl::CaptureAllowed() const {
  return protocol_manager != nullptr && !protocol_manager->SessionLocked();
}

void TreelandProtocolManagerImpl::ApplySurfacePosition(wlr_surface* surface,
                                                       int x, int y) const {
  if (surface == nullptr || compositor == nullptr) return;
  if (auto* toplevel = compositor->ToplevelForSurface(surface);
      toplevel != nullptr && toplevel->scene_tree != nullptr) {
    const wlr_box geometry = toplevel->FrameGeometry();
    wlr_scene_node_set_position(&toplevel->scene_tree->node, x - geometry.x,
                                y - geometry.y);
    protocol_manager->UpdateToplevel(toplevel->Surface());
    return;
  }
  for (const auto& layer : compositor->layer_surfaces_) {
    if (layer->handle != nullptr && layer->scene_surface != nullptr &&
        wlr_surface_get_root_surface(layer->handle->surface) ==
            wlr_surface_get_root_surface(surface)) {
      wlr_scene_node_set_position(&layer->scene_surface->tree->node, x, y);
      return;
    }
  }
}

void TreelandProtocolManagerImpl::ApplySurfaceOverlay(
    wlr_surface* surface) const {
  if (surface == nullptr || compositor == nullptr ||
      compositor->shell_layer_trees_[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY] ==
          nullptr) {
    return;
  }
  if (auto* toplevel = compositor->ToplevelForSurface(surface);
      toplevel != nullptr && toplevel->scene_tree != nullptr) {
    wlr_scene_node_reparent(
        &toplevel->scene_tree->node,
        compositor->shell_layer_trees_[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY]);
  }
}

void TreelandProtocolManagerImpl::ApplySurfaceAutoPlacement(
    wlr_surface* surface, int y_offset) const {
  if (Cursor() == nullptr) return;
  int width = surface == nullptr ? 0 : surface->current.width;
  int height = surface == nullptr ? 0 : surface->current.height;
  int x = static_cast<int>(std::lround(Cursor()->x)) - width / 2;
  int y = static_cast<int>(std::lround(Cursor()->y)) + y_offset;
  if (wlr_output* output = OutputAtCursor(); output != nullptr) {
    wlr_box box = {};
    wlr_output_layout_get_box(output_layout, output, &box);
    x = std::clamp(x, box.x, box.x + std::max(0, box.width - width));
    y = std::clamp(y, box.y, box.y + std::max(0, box.height - height));
  }
  ApplySurfacePosition(surface, x, y);
}

void TreelandProtocolManagerImpl::ActivateSurface(wlr_surface* surface) const {
  if (compositor == nullptr) return;
  compositor->FocusToplevel(compositor->ToplevelForSurface(surface));
}

void TreelandProtocolManagerImpl::SetMinimized(wlr_surface* surface,
                                               bool minimized) const {
  protocol_manager->RequestMinimize(surface, minimized);
}

void TreelandProtocolManagerImpl::SetMaximized(wlr_surface* surface,
                                               bool maximized) const {
  protocol_manager->RequestMaximize(surface, maximized);
}

void TreelandProtocolManagerImpl::SetFullscreen(wlr_surface* surface,
                                                bool fullscreen) const {
  protocol_manager->RequestFullscreen(surface, fullscreen, OutputAtCursor());
}

void TreelandProtocolManagerImpl::SplitSurface(wlr_surface* surface,
                                               bool left) const {
  if (compositor == nullptr || surface == nullptr) return;
  auto* toplevel = compositor->ToplevelForSurface(surface);
  wlr_output* output = OutputAtCursor();
  auto* output_state = compositor->FindOutput(output);
  if (toplevel == nullptr || output_state == nullptr) return;
  wlr_box box = output_state->usable_box;
  box.width /= 2;
  if (!left) box.x += box.width;
  compositor->SetMaximized(toplevel, false);
  toplevel->Configure(box);
}

void TreelandProtocolManagerImpl::SetTitlebar(wlr_surface* surface,
                                              bool enabled) const {
  if (compositor == nullptr) return;
  // Remember the request even before the surface becomes a toplevel, so an
  // xdg-decoration that arrives in between does not attach a titlebar.
  compositor->SetNoTitlebarSurface(surface, !enabled);
  auto* toplevel = compositor->ToplevelForSurface(surface);
  if (toplevel == nullptr) return;
  compositor->SetSsdEnabled(toplevel, enabled);
}

void TreelandProtocolManagerImpl::SetRoundCorner(wlr_surface* surface,
                                                 int radius) const {
  if (compositor == nullptr) return;
  auto* toplevel = compositor->ToplevelForSurface(surface);
  if (toplevel != nullptr) {
    compositor->SetRoundCorner(toplevel, radius);
    return;
  }
  // Personalization is also used by in-process DTK popup surfaces. They are
  // not toplevels, but still need the same renderer mask as their blur.
  compositor->SetSurfaceRoundCorner(surface, radius);
}

void TreelandProtocolManagerImpl::SetShadow(wlr_surface* surface,
                                            bool enabled) const {
  if (compositor == nullptr) {
    return;
  }

  auto* toplevel = compositor->ToplevelForSurface(surface);
  if (toplevel == nullptr) {
    return;
  }
  compositor->SetCsdShadow(toplevel, enabled);
}

void TreelandProtocolManagerImpl::SetBlur(wlr_surface* surface,
                                          bool enabled) const {
  if (surface == nullptr || compositor == nullptr ||
      compositor->backdrop_blur_renderer_ == nullptr) {
    return;
  }
  if (enabled) {
    pixman_region32_t full;
    pixman_region32_init(&full);
    compositor->backdrop_blur_renderer_->SetSurfaceBlur(surface, &full, 2.6F);
    pixman_region32_fini(&full);
  } else {
    compositor->backdrop_blur_renderer_->ClearSurfaceBlur(surface);
  }
  compositor->UpdateBackdropBlurState();
}

bool TreelandProtocolManagerImpl::SurfaceGeometry(wlr_surface* surface,
                                                  wlr_box* geometry) const {
  if (compositor == nullptr || geometry == nullptr) return false;
  auto* toplevel = compositor->ToplevelForSurface(surface);
  if (toplevel == nullptr || toplevel->scene_tree == nullptr ||
      !toplevel->mapped) {
    return false;
  }
  *geometry = toplevel->FrameGeometry();
  geometry->x += toplevel->scene_tree->node.x;
  geometry->y += toplevel->scene_tree->node.y;
  return geometry->width > 0 && geometry->height > 0;
}

bool TreelandProtocolManagerImpl::ClientHasWindowContext(
    wlr_surface* surface) const {
  if (surface == nullptr || surface->resource == nullptr) {
    return false;
  }
  wl_client* client = wl_resource_get_client(surface->resource);
  for (const auto& global : globals) {
    if (global->ClientHasWindowContext(client)) {
      return true;
    }
  }
  return false;
}

}  // namespace protocol
}  // namespace flakewm
