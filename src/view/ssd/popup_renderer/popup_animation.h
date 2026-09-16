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

#ifndef SRC_VIEW_SSD_POPUP_RENDERER_POPUP_ANIMATION_H_
#define SRC_VIEW_SSD_POPUP_RENDERER_POPUP_ANIMATION_H_

#include <QElapsedTimer>
#include <QTimer>
#include <functional>

namespace flakewm {
namespace view {

// Drives both the QML buffer and its compositor-side blur buffer so they
// remain visually locked during GXWM-style popup transitions.
class PopupAnimation final {
 public:
  using Frame = std::function<void(double value, bool finished)>;

  explicit PopupAnimation(Frame frame);

  void Show();
  void Hide();
  void StopAt(double value);

 private:
  void Start(double target, int duration_ms);
  void Tick();

  Frame frame_;
  QTimer timer_;
  QElapsedTimer clock_;
  double value_ = 0.0;
  double from_ = 0.0;
  double target_ = 0.0;
  int duration_ms_ = 1;
};

}  // namespace view
}  // namespace flakewm

#endif  // SRC_VIEW_SSD_POPUP_RENDERER_POPUP_ANIMATION_H_
