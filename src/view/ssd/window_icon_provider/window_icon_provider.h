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
 * Window icon provider used by the SSD QML scene.
 */

#ifndef SRC_VIEW_SSD_WINDOW_ICON_PROVIDER_WINDOW_ICON_PROVIDER_H_
#define SRC_VIEW_SSD_WINDOW_ICON_PROVIDER_WINDOW_ICON_PROVIDER_H_

#include <QQuickImageProvider>

namespace flakewm {
namespace view {

class WindowIconProvider final : public QQuickImageProvider {
 public:
  WindowIconProvider();

  QImage requestImage(const QString& id, QSize* size,
                      const QSize& requested_size) override;
};

}  // namespace view
}  // namespace flakewm

#endif  // SRC_VIEW_SSD_WINDOW_ICON_PROVIDER_WINDOW_ICON_PROVIDER_H_
