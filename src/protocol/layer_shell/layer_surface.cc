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
 * Layer shell lifecycle and scene integration.
 */

#include "src/protocol/layer_shell/layer_surface.h"

#include <absl/log/absl_log.h>

#include <cmath>
#include <memory>
#include <utility>

#include "src/core/compositor_private/compositor_private.h"

namespace flakewm {
namespace core {

LayerSurface::LayerSurface(CompositorPrivate* compositor,
                           wlr_layer_surface_v1* layer_surface)
    : compositor(compositor), handle(layer_surface) {}

void LayerSurface::NotifySurfaceScale(wlr_surface* surface, int, int,
                                      void* data) {
  auto* output = static_cast<CompositorPrivate::Output*>(data);
  if (surface == nullptr || output == nullptr || output->handle == nullptr) {
    return;
  }
  wlr_fractional_scale_v1_notify_scale(surface, output->handle->scale);
  wlr_surface_set_preferred_buffer_scale(
      surface, static_cast<int32_t>(std::ceil(output->handle->scale)));
}

void LayerSurface::NotifyOutputScale() const {
  auto* output =
      handle == nullptr ? nullptr : compositor->FindOutput(handle->output);
  if (handle == nullptr || output == nullptr) {
    return;
  }
  wlr_layer_surface_v1_for_each_surface(handle, NotifySurfaceScale, output);
  wlr_layer_surface_v1_for_each_popup_surface(handle, NotifySurfaceScale,
                                              output);
}

void LayerSurface::UnconstrainPopup(wlr_xdg_popup* popup) const {
  auto* output =
      handle == nullptr ? nullptr : compositor->FindOutput(handle->output);
  if (popup == nullptr || output == nullptr || scene_surface == nullptr) {
    return;
  }

  wlr_box box = {};
  wlr_output_effective_resolution(output->handle, &box.width, &box.height);
  box.x = -scene_surface->tree->node.x;
  box.y = -scene_surface->tree->node.y;
  wlr_xdg_popup_unconstrain_from_box(popup, &box);
}

void LayerSurface::OnMap(LayerSurface* layer_surface, void*) {
  layer_surface->mapped = true;
  layer_surface->NotifyOutputScale();
  layer_surface->compositor->ArrangeLayers(
      layer_surface->compositor->FindOutput(layer_surface->handle->output));
  if (layer_surface->handle->current.keyboard_interactive ==
      ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE) {
    layer_surface->compositor->FocusLayerSurface(layer_surface);
  }
}

void LayerSurface::OnUnmap(LayerSurface* layer_surface, void*) {
  layer_surface->mapped = false;
  if (layer_surface->compositor->seat_->keyboard_state.focused_surface ==
      layer_surface->handle->surface) {
    layer_surface->compositor->FocusNextToplevel(nullptr);
  }
  layer_surface->compositor->ArrangeLayers(
      layer_surface->compositor->FindOutput(layer_surface->handle->output));
}

void LayerSurface::OnCommit(LayerSurface* layer_surface, void*) {
  if (layer_surface->handle == nullptr) {
    return;
  }

  layer_surface->NotifyOutputScale();
  auto* output =
      layer_surface->compositor->FindOutput(layer_surface->handle->output);
  if (layer_surface->handle->initial_commit) {
    layer_surface->compositor->ArrangeLayers(output);
    return;
  }

  if ((layer_surface->handle->current.committed &
       WLR_LAYER_SURFACE_V1_STATE_LAYER) != 0 &&
      output != nullptr) {
    wlr_scene_tree* parent =
        output->LayerTree(layer_surface->handle->current.layer);
    if (parent != nullptr &&
        layer_surface->scene_surface->tree->node.parent != parent) {
      wlr_scene_node_reparent(&layer_surface->scene_surface->tree->node,
                              parent);
    }
  }

  if (layer_surface->handle->current.committed != 0) {
    layer_surface->compositor->ArrangeLayers(output);
  }
  if (layer_surface->mapped &&
      layer_surface->handle->current.keyboard_interactive ==
          ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE) {
    layer_surface->compositor->FocusLayerSurface(layer_surface);
  }
}

void LayerSurface::OnNewPopup(LayerSurface* layer_surface,
                              wlr_xdg_popup* popup) {
  layer_surface->UnconstrainPopup(popup);
  auto state = std::make_unique<CompositorPrivate::Popup>(
      layer_surface->compositor, popup);
  state->scene_tree = wlr_scene_xdg_surface_create(
      layer_surface->scene_surface->tree, popup->base);
  if (state->scene_tree == nullptr) {
    ABSL_LOG(ERROR) << "Failed to create scene tree for layer popup";
    return;
  }

  popup->base->data = state->scene_tree;
  state->commit.Connect(&popup->base->surface->events.commit);
  state->destroy.Connect(&popup->events.destroy);
  layer_surface->compositor->popups_.push_back(std::move(state));
}

void LayerSurface::OnDestroy(LayerSurface* layer_surface, void*) {
  auto* output =
      layer_surface->compositor->FindOutput(layer_surface->handle->output);
  layer_surface->map.Disconnect();
  layer_surface->unmap.Disconnect();
  layer_surface->commit.Disconnect();
  layer_surface->new_popup.Disconnect();
  layer_surface->destroy.Disconnect();
  layer_surface->handle->data = nullptr;
  layer_surface->handle = nullptr;
  layer_surface->scene_surface = nullptr;
  layer_surface->mapped = false;
  layer_surface->compositor->ArrangeLayers(output);
}

void CompositorPrivate::OnNewLayerSurface(CompositorPrivate* compositor,
                                          wlr_layer_surface_v1* handle) {
  if (handle->output == nullptr) {
    handle->output = wlr_output_layout_output_at(compositor->output_layout_,
                                                 compositor->cursor_->x,
                                                 compositor->cursor_->y);
  }
  if (handle->output == nullptr) {
    handle->output =
        wlr_output_layout_get_center_output(compositor->output_layout_);
  }

  Output* output = compositor->FindOutput(handle->output);
  if (output == nullptr) {
    ABSL_LOG(ERROR) << "No output available for new layer surface";
    wlr_layer_surface_v1_destroy(handle);
    return;
  }

  auto layer_surface = std::make_unique<LayerSurface>(compositor, handle);
  layer_surface->scene_surface = wlr_scene_layer_surface_v1_create(
      output->LayerTree(handle->pending.layer), handle);
  if (layer_surface->scene_surface == nullptr) {
    ABSL_LOG(ERROR) << "Failed to create scene tree for layer surface";
    wlr_layer_surface_v1_destroy(handle);
    return;
  }
  handle->data = layer_surface.get();

  layer_surface->map.Connect(&handle->surface->events.map);
  layer_surface->unmap.Connect(&handle->surface->events.unmap);
  layer_surface->commit.Connect(&handle->surface->events.commit);
  layer_surface->new_popup.Connect(&handle->events.new_popup);
  layer_surface->destroy.Connect(&handle->events.destroy);
  layer_surface->NotifyOutputScale();
  compositor->layer_surfaces_.push_back(std::move(layer_surface));
}

}  // namespace core
}  // namespace flakewm
