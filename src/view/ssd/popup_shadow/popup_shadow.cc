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
 * Compositor-drawn shadow + border for DTK xdg_popup surfaces (menus).
 */

#include "src/view/ssd/popup_shadow/popup_shadow.h"

#include <QColor>
#include <QImage>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace flakewm {
namespace view {
namespace {

// gxde-wlcom's TREELAND_XDG_POPUP_* constants: an 8px corner radius, a 10px
// shadow spread offset 2px down, black at 0.18 alpha, and a one-pixel border
// (black at 0.14 alpha on the light theme, white at 0.12 alpha on dark).
constexpr int kRadius = 8;
constexpr int kShadowRadius = 10;
constexpr int kShadowSize = kRadius + kShadowRadius;
// The visible shadow band: the gradient fades from the content edge (radius
// kRadius) to kShadowSize, i.e. spreads kShadowSize - kRadius outward. The
// scene node is offset by this same amount so the content hole aligns flush
// with the popup surface (see the window shadow in ssd_shadow.cc, which uses
// kShadowSize - kRadius as its padding for the same reason).
constexpr int kPadding = kShadowSize - kRadius;
constexpr int kShadowAlpha = 0x2E;  // 0x2E ~= 0.18 * 255
// gxde-wlcom's TREELAND_XDG_POPUP_SHADOW_OFFSET_Y: the drop shadow is pushed
// 2px down, so the shadow spreads only kTopPadding above the content while the
// bottom keeps the full kPadding (gxde-wlcom trims the shadow box's top edge by
// the offset but leaves the bottom at the full shadow size).
constexpr int kShadowOffsetY = 2;
constexpr int kTopPadding = kPadding - kShadowOffsetY;  // 8
// gxde-wlcom's popup border is black at 0.14 on light / white at 0.12 on dark,
// but its texture shader fills the outer pixel solidly, so an anti-aliased
// QPen stroke of the same alpha reads far fainter. Draw the border as a solid
// ring instead and keep the light theme a touch stronger so the 1px edge stays
// clearly visible against the translucent menu face.
constexpr int kBorderLight = 0x33;  // black ~0.20 on the light theme
constexpr int kBorderDark = 0x1F;   // white ~0.12 on the dark theme

// Signed distance from a point (in atlas coordinates) to the rounded content
// corner, positive outside the content. The content hole is shifted up by the
// drop offset (and made that much taller) so the shadow spreads kTopPadding
// above the popup but the full kPadding below it, matching gxde-wlcom's shadow
// box that trims only the top edge.
double ContentDistance(double x, double y) {  // NOLINT(bugprone-easily-swappable-parameters)
  const double left = kShadowSize - kRadius - 0.5;   // 9.5
  const double top = left - kShadowOffsetY;          // 7.5
  const double width = kRadius * 2 + 1;              // 17
  const double height = width + kShadowOffsetY;      // 19
  const double cx = left + width / 2.0, cy = top + height / 2.0;
  const double bx = width / 2.0, by = height / 2.0;
  const double rx = std::abs(x - cx) - bx + kRadius;
  const double ry = std::abs(y - cy) - by + kRadius;
  return std::min(std::max(rx, ry), 0.0) +
         std::hypot(std::max(rx, 0.0), std::max(ry, 0.0)) - kRadius;
}

QImage MakeShadowAtlas() {
  const int atlas_size = kShadowSize * 2;
  QImage atlas(atlas_size, atlas_size, QImage::Format_ARGB32_Premultiplied);
  atlas.fill(Qt::transparent);

  // gxde-wlcom's chameleonShadow(): a Gaussian falloff in the signed distance
  // to the rounded content, alpha = 0.6 * exp(-x^2 / 0.15) * 0.18, sampled at
  // x = (max(distance, 0) + overlap) / shadow_size with
  //   shadow_size = spread + 2 * corner_radius            (26)
  //   overlap     = corner_radius + shadow_size * 0.15    (11.9)
  // (see gxde-wlcom's scene/shaders/decoration.frag). Sampling the distance
  // per-pixel — rather than a corner-centred radial gradient — keeps the
  // falloff uniform along the flat edges; a radial gradient fades the flat
  // edges faster than the corners and looks uneven.
  const double shadow_size = kShadowRadius + 2.0 * kRadius;
  const double overlap = kRadius + shadow_size * 0.15;
  for (int y = 0; y < atlas_size; ++y) {
    for (int x = 0; x < atlas_size; ++x) {
      const double distance = ContentDistance(x + 0.5, y + 0.5);
      int alpha = 0;
      if (distance >= 0.0) {
        const double t = (distance + overlap) / shadow_size;
        alpha = static_cast<int>(
            std::lround(std::exp(-(t * t) / 0.15) * kShadowAlpha * 0.6));
      }
      atlas.setPixel(x, y, qRgba(0, 0, 0, alpha));
    }
  }
  return atlas;
}

QImage MakeBorderAtlas(bool dark) {
  const int atlas_size = kShadowSize * 2;
  QImage atlas(atlas_size, atlas_size, QImage::Format_ARGB32_Premultiplied);
  atlas.fill(Qt::transparent);

  // A one-pixel border around the content, black on the light theme and white
  // on dark (gxde-wlcom's theme-aware popup border color). gxde-wlcom draws it
  // OUTSIDE the blurred face: its decoration surface is inset by the border
  // thickness from the decoration origin and the shader's rounded-corner radius
  // is the content radius plus that border (scene/decoration.c,
  // tex_rgba_ex.frag). Render the same shape here as a solid 1px ring just
  // outside the content boundary: fill a rounded rect one pixel larger than the
  // content (radius kRadius + 1, arc centred on the nine-patch split point)
  // then punch out the content itself (radius kRadius). A centred QPen stroke
  // would instead halve its coverage across the edge and anti-alias to ~half
  // strength, reading as a faint line rather than a crisp edge.
  const QColor border =
      dark ? QColor(255, 255, 255, kBorderDark) : QColor(0, 0, 0, kBorderLight);
  // The ring is drawn one pixel larger than the content's corner tile on every
  // side (radius kRadius + 1). The nine-patch maps that tile onto the content
  // boundary, so the ring survives as the 1px band just outside it — drawing a
  // rect that reaches the atlas edge would instead put its far strips into the
  // shadow's corner region.
  const QRectF outer(kPadding - 1, kPadding - 1, kRadius * 2 + 2,
                     kRadius * 2 + 2);
  const QRectF inner(kPadding, kPadding, kRadius * 2, kRadius * 2);
  QPainter painter(&atlas);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setPen(Qt::NoPen);
  painter.setBrush(border);
  painter.drawRoundedRect(outer, kRadius + 1, kRadius + 1);
  painter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
  painter.setBrush(Qt::black);
  painter.drawRoundedRect(inner, kRadius, kRadius);
  return atlas;
}

void PaintNinePatch(QImage* target, const QImage& source) {
  const int split = kShadowSize;
  const int trailing = source.width() - split - 1;
  const int right = target->width() - trailing;
  const int bottom = target->height() - trailing;

  QPainter painter(target);
  painter.drawImage(QRect(0, 0, split, split), source,
                    QRect(0, 0, split, split));
  painter.drawImage(QRect(split, 0, right - split, split), source,
                    QRect(split, 0, 1, split));
  painter.drawImage(QRect(right, 0, trailing, split), source,
                    QRect(split + 1, 0, trailing, split));
  painter.drawImage(QRect(0, split, split, bottom - split), source,
                    QRect(0, split, split, 1));
  painter.drawImage(QRect(right, split, trailing, bottom - split), source,
                    QRect(split + 1, split, trailing, 1));
  painter.drawImage(QRect(0, bottom, split, trailing), source,
                    QRect(0, split + 1, split, trailing));
  painter.drawImage(QRect(split, bottom, right - split, trailing), source,
                    QRect(split, split + 1, 1, trailing));
  painter.drawImage(QRect(right, bottom, trailing, trailing), source,
                    QRect(split + 1, split + 1, trailing, trailing));
}

}  // namespace

PopupShadow::PopupShadow(wlr_scene_tree* parent) {
  node_ = wlr_scene_buffer_create(parent, nullptr);
  if (node_ != nullptr) {
    node_->point_accepts_input = RejectInput;
    node_destroy_.Connect(&node_->node.events.destroy);
    wlr_scene_node_lower_to_bottom(&node_->node);
    wlr_scene_node_set_enabled(&node_->node, false);
  }
  // The border is a separate node raised above the shadow so its 1px ring stays
  // visible just outside the content edge (gxde-wlcom draws the border around
  // the blurred face, not under it). Drawing it into the shadow atlas would
  // bury it under the (translucent) surface where it only ever contrasts
  // against the shadow.
  border_node_ = wlr_scene_buffer_create(parent, nullptr);
  if (border_node_ != nullptr) {
    border_node_->point_accepts_input = RejectInput;
    border_node_destroy_.Connect(&border_node_->node.events.destroy);
    wlr_scene_node_raise_to_top(&border_node_->node);
    wlr_scene_node_set_enabled(&border_node_->node, false);
  }
}

PopupShadow::~PopupShadow() {
  if (node_ != nullptr) {
    wlr_scene_node_destroy(&node_->node);
  }
  if (border_node_ != nullptr) {
    wlr_scene_node_destroy(&border_node_->node);
  }
  DropBuffer();
  DropBorderBuffer();
}

void PopupShadow::SetDark(bool dark) { dark_ = dark; }

void PopupShadow::Update(const wlr_box& frame) {
  if (node_ == nullptr) {
    return;
  }
  if (frame.width <= 0 || frame.height <= 0) {
    frame_ = frame;
    wlr_scene_node_set_enabled(&node_->node, false);
    if (border_node_ != nullptr) {
      wlr_scene_node_set_enabled(&border_node_->node, false);
    }
    return;
  }
  const bool resized =
      frame_.width != frame.width || frame_.height != frame.height;
  const bool rethemed = dark_ != rendered_dark_;
  if (!resized && !rethemed) {
    wlr_scene_node_set_position(&node_->node, frame.x - kPadding,
                                frame.y - kTopPadding);
    if (border_node_ != nullptr) {
      wlr_scene_node_set_position(&border_node_->node, frame.x - kPadding,
                                  frame.y - kPadding);
    }
    return;
  }

  auto* next = SsdBuffer::Create(frame.width + kPadding * 2,
                                 frame.height + kTopPadding + kPadding);
  if (next == nullptr) {
    wlr_scene_node_set_enabled(&node_->node, false);
    if (border_node_ != nullptr) {
      wlr_scene_node_set_enabled(&border_node_->node, false);
    }
    return;
  }
  PaintNinePatch(&next->Image(), MakeShadowAtlas());
  wlr_scene_buffer_set_buffer_with_damage(node_, next->Handle(), nullptr);
  DropBuffer();
  buffer_ = next;

  if (border_node_ != nullptr) {
    auto* border_next = SsdBuffer::Create(frame.width + kPadding * 2,
                                          frame.height + kPadding * 2);
    if (border_next != nullptr) {
      PaintNinePatch(&border_next->Image(), MakeBorderAtlas(dark_));
      wlr_scene_buffer_set_buffer_with_damage(border_node_,
                                              border_next->Handle(), nullptr);
      DropBorderBuffer();
      border_buffer_ = border_next;
      wlr_scene_node_set_position(&border_node_->node, frame.x - kPadding,
                                  frame.y - kPadding);
      wlr_scene_node_set_enabled(&border_node_->node, true);
    }
  }

  frame_ = frame;
  rendered_dark_ = dark_;
  wlr_scene_node_set_position(&node_->node, frame.x - kPadding,
                              frame.y - kTopPadding);
  wlr_scene_node_set_enabled(&node_->node, true);
}

bool PopupShadow::RejectInput(wlr_scene_buffer*, double*, double*) {
  return false;
}

void PopupShadow::OnNodeDestroy(PopupShadow* shadow, void*) {
  shadow->node_destroy_.Disconnect();
  shadow->node_ = nullptr;
}

void PopupShadow::OnBorderNodeDestroy(PopupShadow* shadow, void*) {
  shadow->border_node_destroy_.Disconnect();
  shadow->border_node_ = nullptr;
}

void PopupShadow::DropBuffer() {
  if (buffer_ != nullptr) {
    wlr_buffer_drop(buffer_->Handle());
    buffer_ = nullptr;
  }
}

void PopupShadow::DropBorderBuffer() {
  if (border_buffer_ != nullptr) {
    wlr_buffer_drop(border_buffer_->Handle());
    border_buffer_ = nullptr;
  }
}

}  // namespace view
}  // namespace flakewm
