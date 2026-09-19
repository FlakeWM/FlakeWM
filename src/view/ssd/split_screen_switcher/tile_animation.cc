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
 * Adapted from GXDE-Wlcom, originally licensed under GPLv3.
 * Code has been modified to fit in Wlroots 0.20.2 & C++.
 * Now re-licensed under GPLv3.
 */

#include "src/view/ssd/split_screen_switcher/tile_animation.h"

#include <algorithm>
#include <cmath>

namespace flakewm {
namespace view {
namespace {

constexpr int kDurationMs = 260;
constexpr int kTickMs = 10;

bool IgnoreInput(wlr_scene_buffer*, double*, double*) { return false; }

double Ease(double value) {
  value = std::clamp(value, 0.0, 1.0);
  return value * value * (3.0 - 2.0 * value);
}

int Interpolate(int from, int to, double progress) {
  return static_cast<int>(std::lround(from + (to - from) * progress));
}

}  // namespace

TileAnimation::TileAnimation(wlr_scene_tree* overlay_parent)
    : overlay_parent_(overlay_parent) {
  timer_.setInterval(kTickMs);
  QObject::connect(&timer_, &QTimer::timeout, [this]() { Update(); });
}

TileAnimation::~TileAnimation() { Complete(true); }

bool TileAnimation::Start(wlr_scene_tree* source, const wlr_box& from,
                          const wlr_box& to) {
  Cancel();
  if (overlay_parent_ == nullptr || source == nullptr || from.width <= 0 ||
      from.height <= 0 || to.width <= 0 || to.height <= 0) {
    return false;
  }
  snapshot_tree_ = wlr_scene_tree_create(overlay_parent_);
  if (snapshot_tree_ == nullptr) return false;
  from_ = from;
  to_ = to;
  CaptureContext context = {
      .animation = this,
      .origin_x = from.x,
      .origin_y = from.y,
  };
  wlr_scene_node_for_each_buffer(&source->node, CaptureBuffer, &context);
  if (snapshots_.empty()) {
    Complete(false);
    return false;
  }
  source_ = source;
  wlr_scene_node_set_enabled(&source_->node, false);
  wlr_scene_node_raise_to_top(&snapshot_tree_->node);
  clock_.restart();
  timer_.start();
  Update();
  return true;
}

void TileAnimation::SourceUnavailable(wlr_scene_tree* source) {
  if (source != nullptr && source == source_) Complete(false);
}

void TileAnimation::Cancel() { Complete(true); }

void TileAnimation::Update() {
  if (snapshot_tree_ == nullptr) return;
  const double raw =
      std::clamp(static_cast<double>(clock_.elapsed()) / kDurationMs, 0.0, 1.0);
  const double progress = Ease(raw);
  const int x = Interpolate(from_.x, to_.x, progress);
  const int y = Interpolate(from_.y, to_.y, progress);
  const int width = std::max(1, Interpolate(from_.width, to_.width, progress));
  const int height =
      std::max(1, Interpolate(from_.height, to_.height, progress));
  const double scale_x = static_cast<double>(width) / from_.width;
  const double scale_y = static_cast<double>(height) / from_.height;
  for (const Snapshot& snapshot : snapshots_) {
    const int node_x = x + static_cast<int>(std::lround(snapshot.x * scale_x));
    const int node_y = y + static_cast<int>(std::lround(snapshot.y * scale_y));
    const int node_width =
        std::max(1, static_cast<int>(std::lround(snapshot.width * scale_x)));
    const int node_height =
        std::max(1, static_cast<int>(std::lround(snapshot.height * scale_y)));
    wlr_scene_node_set_position(&snapshot.node->node, node_x, node_y);
    wlr_scene_buffer_set_dest_size(snapshot.node, node_width, node_height);
  }
  if (raw >= 1.0) Complete(true);
}

void TileAnimation::Complete(bool restore_source) {
  timer_.stop();
  if (restore_source && source_ != nullptr) {
    wlr_scene_node_set_enabled(&source_->node, true);
  }
  source_ = nullptr;
  snapshots_.clear();
  if (snapshot_tree_ != nullptr) {
    wlr_scene_node_destroy(&snapshot_tree_->node);
    snapshot_tree_ = nullptr;
  }
}

void TileAnimation::CaptureBuffer(wlr_scene_buffer* buffer, int x, int y,
                                  void* data) {
  auto* context = static_cast<CaptureContext*>(data);
  if (context == nullptr || context->animation == nullptr ||
      buffer == nullptr || buffer->buffer == nullptr) {
    return;
  }
  wlr_scene_buffer* clone = wlr_scene_buffer_create(
      context->animation->snapshot_tree_, buffer->buffer);
  if (clone == nullptr) return;
  clone->point_accepts_input = IgnoreInput;
  wlr_scene_buffer_set_source_box(clone, &buffer->src_box);
  wlr_scene_buffer_set_transform(clone, buffer->transform);
  wlr_scene_buffer_set_filter_mode(clone, buffer->filter_mode);
  wlr_scene_buffer_set_opacity(clone, buffer->opacity);
  const int width = buffer->dst_width > 0 ? buffer->dst_width
                                          : buffer->WLR_PRIVATE.buffer_width;
  const int height = buffer->dst_height > 0 ? buffer->dst_height
                                            : buffer->WLR_PRIVATE.buffer_height;
  context->animation->snapshots_.push_back({
      .node = clone,
      .x = x - context->origin_x,
      .y = y - context->origin_y,
      .width = std::max(1, width),
      .height = std::max(1, height),
  });
}

}  // namespace view
}  // namespace flakewm
