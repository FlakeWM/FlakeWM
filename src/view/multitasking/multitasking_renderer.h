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
 * The layout is adapted from GXDE KWin's Multitasking screen.
 */

#ifndef SRC_VIEW_MULTITASKING_MULTITASKING_RENDERER_H_
#define SRC_VIEW_MULTITASKING_MULTITASKING_RENDERER_H_

#include <QMetaObject>
#include <QVariant>
#include <array>
#include <memory>
#include <vector>

#include "src/view/multitasking/multitasking.h"

class QQuickItem;
class QQuickRenderControl;
class QQuickWindow;

namespace flakewm {
namespace view {

class SsdBuffer;

class MultitaskingRenderer final {
 public:
  MultitaskingRenderer();
  ~MultitaskingRenderer();

  bool Resize(int width, int height);
  void SetState(const std::vector<Multitasking::Entry>& entries,
                const std::vector<int>& workspace_window_counts,
                const std::vector<MultitaskingWindowPlacement>& placements,
                int current_workspace, int selected_workspace,
                int selected_window, int hovered_workspace, int hovered_window,
                double workspace_opacity, const MultitaskingLayout& layout);
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

#endif  // SRC_VIEW_MULTITASKING_MULTITASKING_RENDERER_H_
