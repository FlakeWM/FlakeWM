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
 * Chameleon-compatible window shadow.
 */

#include "src/view/ssd/ssd_shadow/ssd_shadow.h"

#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QRadialGradient>
#include <cmath>

namespace flakewm {
namespace view {
namespace {

constexpr int kRadius = 8;
constexpr int kShadowRadius = 40;
constexpr int kShadowSize = kShadowRadius + kRadius * 2;
constexpr int kHorizontalPadding = kShadowSize - kRadius;
constexpr int kBottomPadding = kShadowSize - kRadius;

QColor WithAlpha(QColor color, int alpha) {
  color.setAlpha(alpha);
  return color;
}

QColor BorderColor(bool dialog) {
  return dialog ? QColor(0, 0, 0, 0x1a) : QColor(0, 0, 0, 0x33);
}

QImage MakeAtlas(bool active, bool dialog) {
  const int offset_y = dialog ? 20 : 30;
  const int strength = dialog ? 0x33 : (active ? 0x80 : 0x40);
  QImage atlas(kShadowSize * 2, kShadowSize * 2,
               QImage::Format_ARGB32_Premultiplied);
  atlas.fill(Qt::transparent);

  QRadialGradient gradient(kShadowSize, kShadowSize, kShadowSize);
  for (int index = 0; index < 10; ++index) {
    const qreal distance = static_cast<qreal>(index) / 9.0;
    const int alpha = std::exp(-distance * distance / 0.15) * strength * 0.6;
    gradient.setColorAt(distance, WithAlpha(Qt::black, alpha));
  }
  gradient.setColorAt(1.0, Qt::transparent);

  QPainter painter(&atlas);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.fillRect(atlas.rect(), gradient);

  const QRectF hole(kShadowSize - kRadius - 0.5,
                    kShadowSize - offset_y - kRadius - 0.5, kRadius * 2 + 1,
                    offset_y + kRadius * 2 + 1);
  painter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
  painter.setPen(Qt::NoPen);
  painter.setBrush(Qt::black);
  painter.drawRoundedRect(hole, kRadius, kRadius);

  painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
  painter.setPen(QPen(BorderColor(dialog), 1.0));
  painter.setBrush(Qt::NoBrush);
  painter.drawRoundedRect(hole, kRadius + 0.5, kRadius + 0.5);
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

SsdShadow::SsdShadow(wlr_scene_tree* parent) {
  node_ = wlr_scene_buffer_create(parent, nullptr);
  if (node_ != nullptr) {
    node_->point_accepts_input = RejectInput;
    node_destroy_.Connect(&node_->node.events.destroy);
    wlr_scene_node_lower_to_bottom(&node_->node);
    wlr_scene_node_set_enabled(&node_->node, false);
  }
}

SsdShadow::~SsdShadow() {
  if (node_ != nullptr) {
    wlr_scene_node_destroy(&node_->node);
  }
  DropBuffer();
}

void SsdShadow::Update(const wlr_box& frame, bool active, bool dialog,
                       bool maximized) {
  if (node_ == nullptr) {
    return;
  }
  if (maximized || frame.width <= 0 || frame.height <= 0) {
    maximized_ = maximized;
    wlr_scene_node_set_enabled(&node_->node, false);
    return;
  }
  if (frame_.width == frame.width && frame_.height == frame.height &&
      active_ == active && dialog_ == dialog && maximized_ == maximized) {
    wlr_scene_node_set_position(&node_->node, frame.x - kHorizontalPadding,
                                frame.y - (dialog ? 28 : 18));
    return;
  }

  const int top_padding = dialog ? 28 : 18;
  auto* next = SsdBuffer::Create(frame.width + kHorizontalPadding * 2,
                                 frame.height + top_padding + kBottomPadding);
  if (next == nullptr) {
    wlr_scene_node_set_enabled(&node_->node, false);
    return;
  }
  PaintNinePatch(&next->Image(), MakeAtlas(active, dialog));
  wlr_scene_buffer_set_buffer_with_damage(node_, next->Handle(), nullptr);
  DropBuffer();
  buffer_ = next;
  frame_ = frame;
  active_ = active;
  dialog_ = dialog;
  maximized_ = maximized;
  wlr_scene_node_set_position(&node_->node, frame.x - kHorizontalPadding,
                              frame.y - top_padding);
  wlr_scene_node_set_enabled(&node_->node, true);
}

bool SsdShadow::RejectInput(wlr_scene_buffer*, double*, double*) {
  return false;
}

void SsdShadow::OnNodeDestroy(SsdShadow* shadow, void*) {
  shadow->node_destroy_.Disconnect();
  shadow->node_ = nullptr;
}

void SsdShadow::DropBuffer() {
  if (buffer_ != nullptr) {
    wlr_buffer_drop(buffer_->Handle());
    buffer_ = nullptr;
  }
}

}  // namespace view
}  // namespace flakewm
