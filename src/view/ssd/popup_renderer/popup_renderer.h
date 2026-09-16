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

#ifndef SRC_VIEW_SSD_POPUP_RENDERER_POPUP_RENDERER_H_
#define SRC_VIEW_SSD_POPUP_RENDERER_POPUP_RENDERER_H_

#include <QMetaObject>
#include <QVariant>
#include <memory>

#include "src/wlr_wrapper/wlroots.h"

class QQuickItem;
class QQuickRenderControl;
class QQuickWindow;

namespace flakewm {
namespace view {

class SsdBuffer;

class PopupRenderer final {
 public:
  explicit PopupRenderer(const char* resource_url);
  ~PopupRenderer();

  PopupRenderer(const PopupRenderer&) = delete;
  PopupRenderer& operator=(const PopupRenderer&) = delete;

  bool IsValid() const;
  bool Resize(int width, int height);
  void SetProperty(const char* name, const QVariant& value);
  bool Render();
  wlr_buffer* Buffer() const;

 private:
  void DropBuffers();

  std::unique_ptr<QQuickRenderControl> render_control_;
  std::unique_ptr<QQuickWindow> window_;
  QQuickItem* root_item_ = nullptr;
  SsdBuffer* buffers_[2] = {};
  QMetaObject::Connection render_requested_;
  QMetaObject::Connection scene_changed_;
  int width_ = 0;
  int height_ = 0;
  int current_buffer_ = 0;
  bool has_frame_ = false;
  bool initialized_ = false;
  bool dirty_ = true;
};

}  // namespace view
}  // namespace flakewm

#endif  // SRC_VIEW_SSD_POPUP_RENDERER_POPUP_RENDERER_H_
