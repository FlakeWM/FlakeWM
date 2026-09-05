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
 * Bottom-corner clipping for a server-decorated surface.
 */

#include "src/view/ssd/ssd_surface_clip/ssd_surface_clip.h"

#include <memory>
#include <utility>

namespace flakewm {
namespace view {
namespace {

constexpr int kSamples = 16;

float PixelCoverage(int row, int column) {
  int inside = 0;
  for (int sample_y = 0; sample_y < kSamples; ++sample_y) {
    const double y = row - 1.0 + (sample_y + 0.5) / kSamples;
    for (int sample_x = 0; sample_x < kSamples; ++sample_x) {
      const double x = 1.0 + column + (sample_x + 0.5) / kSamples - 8.0;
      if (y <= 0.0 || x >= 0.0 || x * x + y * y <= 64.0) {
        ++inside;
      }
    }
  }
  return static_cast<float>(inside) / (kSamples * kSamples);
}

wlr_scene_buffer* RootBuffer(wlr_scene_tree* tree) {
  if (tree == nullptr || wl_list_empty(&tree->children)) {
    return nullptr;
  }
  wlr_scene_node* first = nullptr;
  first = wl_container_of(tree->children.next, first, link);
  return first->type == WLR_SCENE_NODE_BUFFER
             ? wlr_scene_buffer_from_node(first)
             : nullptr;
}

}  // namespace

std::unique_ptr<SsdSurfaceClip> SsdSurfaceClip::Create(wlr_scene_tree* xdg_tree,
                                                       wlr_surface* surface) {
  if (xdg_tree == nullptr || surface == nullptr ||
      wl_list_empty(&xdg_tree->children)) {
    return nullptr;
  }

  wlr_scene_node* first = nullptr;
  first = wl_container_of(xdg_tree->children.next, first, link);
  if (first->type != WLR_SCENE_NODE_TREE) {
    return nullptr;
  }
  wlr_scene_tree* surface_tree = nullptr;
  surface_tree = wl_container_of(first, surface_tree, node);

  auto clip = std::unique_ptr<SsdSurfaceClip>(
      new SsdSurfaceClip(xdg_tree, surface_tree, surface));
  return clip->Initialize() ? std::move(clip) : nullptr;
}

SsdSurfaceClip::SsdSurfaceClip(wlr_scene_tree* xdg_tree,
                               wlr_scene_tree* surface_tree,
                               wlr_surface* surface)
    : xdg_tree_(xdg_tree), surface_tree_(surface_tree), surface_(surface) {
  tree_destroy_.Connect(&xdg_tree_->node.events.destroy);
}

SsdSurfaceClip::~SsdSurfaceClip() {
  commit_.Disconnect();
  ClearClip();
  if (xdg_tree_ != nullptr) {
    for (Segment& segment : segments_) {
      if (segment.tree != nullptr) {
        wlr_scene_node_destroy(&segment.tree->node);
        segment.tree = nullptr;
        segment.buffer = nullptr;
      }
    }
  }
}

bool SsdSurfaceClip::Initialize() {
  auto add_segment = [this](Segment::Alignment alignment, int row, int inset,
                            float coverage) {
    Segment segment = {
        .tree = wlr_scene_subsurface_tree_create(xdg_tree_, surface_),
        .alignment = alignment,
        .row = row,
        .inset = inset,
        .coverage = coverage,
    };
    if (segment.tree == nullptr) {
      return false;
    }
    segment.buffer = RootBuffer(segment.tree);
    if (segment.buffer == nullptr) {
      wlr_scene_node_destroy(&segment.tree->node);
      return false;
    }
    wlr_scene_node_set_enabled(&segment.tree->node, false);
    segments_.push_back(segment);
    return true;
  };

  for (int row = 0; row < kRadius; ++row) {
    int opaque_inset = kRadius;
    for (int column = 0; column < kRadius; ++column) {
      const float coverage = PixelCoverage(row, column);
      if (coverage >= 0.999F) {
        opaque_inset = column;
        break;
      }
      if (coverage > 0.001F &&
          (!add_segment(Segment::Alignment::kLeft, row, column, coverage) ||
           !add_segment(Segment::Alignment::kRight, row, column, coverage))) {
        return false;
      }
    }
    if (!add_segment(Segment::Alignment::kCenter, row, opaque_inset, 1.0F)) {
      return false;
    }
  }
  commit_.Connect(&surface_->events.commit);
  ApplyCoverage();
  return true;
}

void SsdSurfaceClip::Update(const wlr_box& geometry, bool maximized) {
  if (xdg_tree_ == nullptr || surface_tree_ == nullptr) {
    return;
  }
  if (maximized || geometry.width <= kRadius * 2 ||
      geometry.height <= kRadius) {
    ClearClip();
    return;
  }

  const wlr_box body = {
      .x = geometry.x,
      .y = geometry.y,
      .width = geometry.width,
      .height = geometry.height - kRadius,
  };
  wlr_scene_subsurface_tree_set_clip(&surface_tree_->node, &body);

  for (Segment& segment : segments_) {
    const bool right = segment.alignment == Segment::Alignment::kRight;
    const bool center = segment.alignment == Segment::Alignment::kCenter;
    const int x = right ? geometry.width - segment.inset - 1 : segment.inset;
    const wlr_box segment_clip = {
        .x = geometry.x + x,
        .y = geometry.y + geometry.height - kRadius + segment.row,
        .width = center ? geometry.width - segment.inset * 2 : 1,
        .height = 1,
    };
    wlr_scene_node_set_position(&segment.tree->node, -geometry.x, -geometry.y);
    wlr_scene_subsurface_tree_set_clip(&segment.tree->node, &segment_clip);
    wlr_scene_node_set_enabled(&segment.tree->node, true);
  }
}

void SsdSurfaceClip::ClearClip() {
  if (surface_tree_ != nullptr) {
    wlr_scene_subsurface_tree_set_clip(&surface_tree_->node, nullptr);
  }
  for (const Segment& segment : segments_) {
    if (segment.tree != nullptr) {
      wlr_scene_node_set_enabled(&segment.tree->node, false);
    }
  }
}

void SsdSurfaceClip::ApplyCoverage() {
  for (const Segment& segment : segments_) {
    if (segment.buffer != nullptr && segment.coverage < 1.0F) {
      wlr_scene_buffer_set_opacity(segment.buffer,
                                   segment.buffer->opacity * segment.coverage);
    }
  }
}

void SsdSurfaceClip::OnCommit(SsdSurfaceClip* clip, void*) {
  clip->ApplyCoverage();
}

void SsdSurfaceClip::OnTreeDestroy(SsdSurfaceClip* clip, void*) {
  clip->commit_.Disconnect();
  clip->tree_destroy_.Disconnect();
  clip->xdg_tree_ = nullptr;
  clip->surface_tree_ = nullptr;
  clip->surface_ = nullptr;
  clip->segments_.clear();
}

}  // namespace view
}  // namespace flakewm
