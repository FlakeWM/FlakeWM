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
#include <QPainter>
#include <QRadialGradient>

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
// gxde-wlcom's popup border is black at 0.14 on light / white at 0.12 on dark,
// but its texture shader fills the outer pixel solidly, so an anti-aliased
// QPen stroke of the same alpha reads far fainter. Draw the border as a solid
// ring instead and keep the light theme a touch stronger so the 1px edge stays
// clearly visible against the translucent menu face.
constexpr int kBorderLight = 0x33;  // black ~0.20 on the light theme
constexpr int kBorderDark = 0x1F;   // white ~0.12 on the dark theme

// The rounded content hole shared by both atlases. The odd size (+1) and the
// -0.5 offset keep the hole symmetric about the nine-patch split point, so its
// flat edges stay flush with the popup surface after stretching.
QRectF Hole() {
  return QRectF(kShadowSize - kRadius - 0.5, kShadowSize - kRadius - 0.5,
                kRadius * 2 + 1, kRadius * 2 + 1);
}

QImage MakeShadowAtlas() {
  const int atlas_size = kShadowSize * 2;
  QImage atlas(atlas_size, atlas_size, QImage::Format_ARGB32_Premultiplied);
  atlas.fill(Qt::transparent);

  // Shadow fades from full strength at the content edge (radius kRadius) to
  // transparent at the outer spread (radius kShadowSize).
  const int center = kShadowSize;
  QRadialGradient gradient(center, center, kShadowSize, center, center,
                           kRadius);
  gradient.setColorAt(0.0, QColor(0, 0, 0, kShadowAlpha));
  gradient.setColorAt(1.0, Qt::transparent);

  QPainter painter(&atlas);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.fillRect(atlas.rect(), gradient);

  // Punch the rounded content hole out of the shadow.
  painter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
  painter.setPen(Qt::NoPen);
  painter.setBrush(Qt::black);
  painter.drawRoundedRect(Hole(), kRadius, kRadius);
  return atlas;
}

QImage MakeBorderAtlas(bool dark) {
  const int atlas_size = kShadowSize * 2;
  QImage atlas(atlas_size, atlas_size, QImage::Format_ARGB32_Premultiplied);
  atlas.fill(Qt::transparent);

  // A one-pixel border along the content edge, black on the light theme and
  // white on dark (gxde-wlcom's theme-aware popup border color). gxde-wlcom's
  // texture shader fills the outer pixel of the surface solidly (dist in
  // [-borderWidth, 0] at full border alpha — see tex_rgba_ex.frag), so render
  // the same shape here as a solid 1px ring: fill the content boundary (radius
  // kRadius, arc centred on the nine-patch split point) then punch out the
  // 1px-inset interior. A centred QPen stroke would halve its coverage across
  // the content edge and anti-alias to ~half strength, reading as a faint line
  // sitting inside the menu rather than a crisp edge on it.
  const QColor border =
      dark ? QColor(255, 255, 255, kBorderDark) : QColor(0, 0, 0, kBorderLight);
  // The ring is drawn on the content's 16x16 corner tile (radius kRadius, arc
  // centred on the nine-patch split point). The nine-patch then maps that tile
  // onto the full content boundary, so only the corner arc and the two flat
  // edges survive — drawing a rect that reaches the atlas edge would instead
  // put its far strips into the shadow's corner region.
  const QRectF outer(kPadding, kPadding, kRadius * 2, kRadius * 2);
  const QRectF inner(kPadding + 1, kPadding + 1, kRadius * 2 - 2,
                     kRadius * 2 - 2);
  QPainter painter(&atlas);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setPen(Qt::NoPen);
  painter.setBrush(border);
  painter.drawRoundedRect(outer, kRadius, kRadius);
  painter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
  painter.setBrush(Qt::black);
  painter.drawRoundedRect(inner, kRadius - 1, kRadius - 1);
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
  // The border is a separate node raised above the popup surface so its 1px
  // stroke is visible on the content edge. Drawing it into the shadow atlas
  // would bury it under the (translucent) surface where it only ever contrasts
  // against the shadow — gxde-wlcom instead draws the border over the content.
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
                                frame.y - kPadding);
    if (border_node_ != nullptr) {
      wlr_scene_node_set_position(&border_node_->node, frame.x - kPadding,
                                  frame.y - kPadding);
    }
    return;
  }

  auto* next = SsdBuffer::Create(frame.width + kPadding * 2,
                                 frame.height + kPadding * 2);
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
                              frame.y - kPadding);
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
