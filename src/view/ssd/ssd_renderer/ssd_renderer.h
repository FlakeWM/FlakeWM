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
 * QtQuick renderer for an SSD titlebar.
 */

#ifndef SRC_VIEW_SSD_SSD_RENDERER_SSD_RENDERER_H_
#define SRC_VIEW_SSD_SSD_RENDERER_SSD_RENDERER_H_

#include <QMetaObject>
#include <QString>
#include <array>
#include <memory>

#include "src/view/ssd/ssd_buffer/ssd_buffer.h"

class QQmlEngine;
class QQuickItem;
class QQuickRenderControl;
class QQuickWindow;
class QVariant;

namespace flakewm {
namespace view {

class SsdRenderer final {
 public:
  SsdRenderer();
  ~SsdRenderer();

  SsdRenderer(const SsdRenderer&) = delete;
  SsdRenderer& operator=(const SsdRenderer&) = delete;

  bool IsValid() const;
  bool Resize(int width, int height);
  void SetActive(bool active);
  void SetMaximized(bool maximized);
  void SetDialog(bool dialog);
  void SetCanMinimize(bool can_minimize);
  void SetCanMaximize(bool can_maximize);
  void SetTitle(const QString& title);
  void SetAppId(const QString& app_id);
  void SetHoveredPart(int part);
  void SetPressedPart(int part);
  bool Render();
  wlr_buffer* Buffer() const;

 private:
  static QQmlEngine* Engine();
  void DropBuffers();
  void SetProperty(const char* name, const QVariant& value);

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

#endif  // SRC_VIEW_SSD_SSD_RENDERER_SSD_RENDERER_H_
