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

#ifndef SRC_VIEW_SSD_WINDOW_MENU_WINDOW_MENU_RENDERER_H_
#define SRC_VIEW_SSD_WINDOW_MENU_WINDOW_MENU_RENDERER_H_

#include <QMetaObject>
#include <QVariantList>
#include <array>
#include <memory>

#include "src/wlr_wrapper/wlroots.h"

class QQuickItem;
class QQuickRenderControl;
class QQuickWindow;

namespace flakewm {
namespace view {

class SsdBuffer;

class WindowMenuRenderer final {
 public:
  static constexpr int kShadowMargin = 10;
  static constexpr int kContentWidth = 270;
  static constexpr int kContentMargin = 12;
  static constexpr int kItemHeight = 26;
  static constexpr int kItemCount = 9;
  static constexpr int kContentHeight =
      kContentMargin * 2 + kItemHeight * kItemCount;
  static constexpr int kWidth = kContentWidth + kShadowMargin * 2;
  static constexpr int kHeight = kContentHeight + kShadowMargin * 2 + 2;

  WindowMenuRenderer();
  ~WindowMenuRenderer();

  WindowMenuRenderer(const WindowMenuRenderer&) = delete;
  WindowMenuRenderer& operator=(const WindowMenuRenderer&) = delete;

  bool IsValid() const;
  void SetItems(const QVariantList& items);
  void SetInteraction(int hovered_index, int pressed_index);
  bool Render();
  wlr_buffer* Buffer() const;

 private:
  void SetProperty(const char* name, const QVariant& value);
  void DropBuffers();

  std::unique_ptr<QQuickRenderControl> render_control_;
  std::unique_ptr<QQuickWindow> window_;
  QQuickItem* root_item_ = nullptr;
  std::array<SsdBuffer*, 2> buffers_ = {};
  QMetaObject::Connection render_requested_;
  QMetaObject::Connection scene_changed_;
  int current_buffer_ = 0;
  bool has_frame_ = false;
  bool initialized_ = false;
  bool dirty_ = true;
};

}  // namespace view
}  // namespace flakewm

#endif  // SRC_VIEW_SSD_WINDOW_MENU_WINDOW_MENU_RENDERER_H_
