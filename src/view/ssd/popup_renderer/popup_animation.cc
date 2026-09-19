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

#include "src/view/ssd/popup_renderer/popup_animation.h"

#include <algorithm>
#include <utility>

namespace flakewm {
namespace view {
namespace {

constexpr int kTickMs = 10;
constexpr int kFadeInMs = 200;
constexpr int kFadeOutMs = 150;

double EaseInOutQuad(double value) {
  value = std::clamp(value, 0.0, 1.0);
  return value < 0.5
             ? 2.0 * value * value
             : 1.0 - ((-2.0 * value + 2.0) * (-2.0 * value + 2.0)) / 2.0;
}

}  // namespace

PopupAnimation::PopupAnimation(Frame frame) : frame_(std::move(frame)) {
  timer_.setInterval(kTickMs);
  QObject::connect(&timer_, &QTimer::timeout, [this]() { Tick(); });
}

void PopupAnimation::Show() { Start(1.0, kFadeInMs); }

void PopupAnimation::Hide() { Start(0.0, kFadeOutMs); }

void PopupAnimation::StopAt(double value) {
  timer_.stop();
  value_ = std::clamp(value, 0.0, 1.0);
  from_ = value_;
  target_ = value_;
  if (frame_) frame_(value_, true);
}

void PopupAnimation::Start(double target, int duration_ms) {
  if (timer_.isActive() && target_ == target) return;
  if (!timer_.isActive() && value_ == target) {
    if (frame_) frame_(value_, true);
    return;
  }
  from_ = value_;
  target_ = target;
  duration_ms_ = std::max(1, duration_ms);
  clock_.restart();
  timer_.start();
  if (frame_) frame_(value_, false);
}

void PopupAnimation::Tick() {
  const double raw = std::clamp(
      static_cast<double>(clock_.elapsed()) / duration_ms_, 0.0, 1.0);
  value_ = from_ + (target_ - from_) * EaseInOutQuad(raw);
  const bool finished = raw >= 1.0;
  if (finished) {
    value_ = target_;
    timer_.stop();
  }
  if (frame_) frame_(value_, finished);
}

}  // namespace view
}  // namespace flakewm
