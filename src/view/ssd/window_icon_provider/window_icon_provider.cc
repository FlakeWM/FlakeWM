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

#include "src/view/ssd/window_icon_provider/window_icon_provider.h"

#include <QIcon>
#include <QImage>
#include <QStringList>

namespace flakewm {
namespace view {

WindowIconProvider::WindowIconProvider()
    : QQuickImageProvider(QQuickImageProvider::Image) {}

QImage WindowIconProvider::requestImage(const QString& id, QSize* size,
                                        const QSize& requested_size) {
  const int extent = requested_size.isValid() ? requested_size.width() : 32;
  const QString base_name = id.section('.', -1).toLower();
  const QStringList names = {id, base_name, base_name + "-qt6",
                             base_name + "-qt5"};
  for (const QString& name : names) {
    if (!QIcon::hasThemeIcon(name)) {
      continue;
    }
    const QImage image =
        QIcon::fromTheme(name).pixmap(extent, extent).toImage();
    if (size != nullptr) {
      *size = image.size();
    }
    return image;
  }
  if (size != nullptr) {
    *size = {};
  }
  return {};
}

}  // namespace view
}  // namespace flakewm
