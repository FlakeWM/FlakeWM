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
 * Server-side window decoration.
 */

#include "src/view/ssd/ssd/ssd.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

namespace flakewm {
namespace view {
namespace {

constexpr int kTitlebarHeight = 40;
constexpr int kBorderWidth = 1;
constexpr int kButtonWidth = 40;
}  // namespace

std::unique_ptr<Ssd> Ssd::Create(wlr_scene_tree* parent) {
  if (parent == nullptr) {
    return nullptr;
  }

  auto ssd = std::unique_ptr<Ssd>(new Ssd(parent));
  if (!ssd->Initialize()) {
    return nullptr;
  }
  return ssd;
}

Ssd::Ssd(wlr_scene_tree* parent) {
  tree_ = wlr_scene_tree_create(parent);
  if (tree_ != nullptr) {
    tree_destroy_.Connect(&tree_->node.events.destroy);
  }
}

Ssd::~Ssd() {
  if (tree_ != nullptr) {
    wlr_scene_node_destroy(&tree_->node);
  }
}

bool Ssd::Initialize() {
  if (tree_ == nullptr || !renderer_.IsValid() ||
      !renderer_.Resize(1, kTitlebarHeight + kBorderWidth) ||
      !renderer_.Render()) {
    return false;
  }

  shadow_ = std::make_unique<SsdShadow>(tree_);
  titlebar_ = wlr_scene_buffer_create(tree_, renderer_.Buffer());
  return titlebar_ != nullptr;
}

void Ssd::SetGeometry(const wlr_box& client_geometry) {
  if (client_geometry_.x == client_geometry.x &&
      client_geometry_.y == client_geometry.y &&
      client_geometry_.width == client_geometry.width &&
      client_geometry_.height == client_geometry.height) {
    return;
  }
  client_geometry_ = client_geometry;
  Arrange();
}

void Ssd::SetActive(bool active) {
  if (active_ == active) {
    return;
  }
  active_ = active;
  renderer_.SetActive(active);
  if (shadow_ != nullptr) {
    shadow_->Update(FrameGeometry(client_geometry_), active_, dialog_,
                    maximized_);
  }
}

void Ssd::SetMaximized(bool maximized) {
  if (maximized_ == maximized) {
    return;
  }
  maximized_ = maximized;
  renderer_.SetMaximized(maximized);
  Arrange();
}

void Ssd::SetDialog(bool dialog) {
  if (dialog_ == dialog) {
    return;
  }
  dialog_ = dialog;
  renderer_.SetDialog(dialog);
  Arrange();
}

void Ssd::SetCapabilities(bool can_minimize, bool can_maximize) {
  can_minimize_ = can_minimize;
  can_maximize_ = can_maximize;
  renderer_.SetCanMinimize(can_minimize);
  renderer_.SetCanMaximize(can_maximize);
}

void Ssd::SetTitle(const std::string& title) {
  renderer_.SetTitle(QString::fromUtf8(title));
}

void Ssd::SetAppId(const std::string& app_id) {
  renderer_.SetAppId(QString::fromUtf8(app_id));
}

void Ssd::SetHovered(HitTarget target) {
  renderer_.SetHoveredPart(static_cast<int>(target.part));
}

void Ssd::SetPressed(HitTarget target) {
  renderer_.SetPressedPart(static_cast<int>(target.part));
}

void Ssd::Render() {
  if (titlebar_ != nullptr && renderer_.Render()) {
    wlr_scene_buffer_set_buffer_with_damage(titlebar_, renderer_.Buffer(),
                                            nullptr);
  }
}

wlr_box Ssd::FrameGeometry(const wlr_box& client_geometry) const {
  const int border = BorderWidth();
  return {
      .x = client_geometry.x - border,
      .y = client_geometry.y - kTitlebarHeight - border,
      .width = client_geometry.width + border * 2,
      .height = client_geometry.height + kTitlebarHeight + border * 2,
  };
}

wlr_box Ssd::ContentGeometry(const wlr_box& frame_geometry) const {
  const int border = BorderWidth();
  return {
      .x = frame_geometry.x + border,
      .y = frame_geometry.y + kTitlebarHeight + border,
      .width = std::max(1, frame_geometry.width - border * 2),
      .height =
          std::max(1, frame_geometry.height - kTitlebarHeight - border * 2),
  };
}

Ssd::HitTarget Ssd::HitTest(double x, double y) const {
  const wlr_box frame = FrameGeometry(client_geometry_);
  if (x < frame.x || y < frame.y || x >= frame.x + frame.width ||
      y >= frame.y + frame.height) {
    return {};
  }

  const int border = BorderWidth();
  if (border > 0) {
    uint32_t edges = 0;
    if (x < frame.x + border) {
      edges |= WLR_EDGE_LEFT;
    } else if (x >= frame.x + frame.width - border) {
      edges |= WLR_EDGE_RIGHT;
    }
    if (y < frame.y + border) {
      edges |= WLR_EDGE_TOP;
    } else if (y >= frame.y + frame.height - border) {
      edges |= WLR_EDGE_BOTTOM;
    }
    if (edges != 0) {
      return {.part = Part::kResize, .edges = edges};
    }
  }

  if (y >= client_geometry_.y) {
    return {};
  }
  const double close_x = frame.x + frame.width - border - kButtonWidth;
  if (x >= close_x) {
    return {.part = Part::kClose};
  }
  double next_button_x = close_x;
  if (can_maximize_) {
    next_button_x -= kButtonWidth;
    if (x >= next_button_x) {
      return {.part = Part::kMaximize};
    }
  }
  if (can_minimize_) {
    next_button_x -= kButtonWidth;
    if (x >= next_button_x) {
      return {.part = Part::kMinimize};
    }
  }
  return {.part = Part::kTitlebar};
}

int Ssd::TitlebarHeight() const { return kTitlebarHeight; }

void Ssd::Arrange() {
  if (tree_ == nullptr || client_geometry_.width <= 0 ||
      client_geometry_.height <= 0) {
    if (tree_ != nullptr) {
      wlr_scene_node_set_enabled(&tree_->node, false);
    }
    return;
  }

  const wlr_box frame = FrameGeometry(client_geometry_);
  if (shadow_ != nullptr) {
    shadow_->Update(frame, active_, dialog_, maximized_);
  }
  wlr_scene_node_set_enabled(&tree_->node, true);
  wlr_scene_node_set_position(&titlebar_->node, client_geometry_.x,
                              client_geometry_.y - kTitlebarHeight);
  if (!renderer_.Resize(client_geometry_.width, kTitlebarHeight)) {
    wlr_scene_node_set_enabled(&tree_->node, false);
    return;
  }
  Render();
}

int Ssd::BorderWidth() const { return maximized_ ? 0 : kBorderWidth; }

void Ssd::OnTreeDestroy(Ssd* ssd, void*) {
  ssd->tree_destroy_.Disconnect();
  ssd->tree_ = nullptr;
  ssd->titlebar_ = nullptr;
}

}  // namespace view
}  // namespace flakewm
