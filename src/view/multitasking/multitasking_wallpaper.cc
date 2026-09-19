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

#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>
#include <QStringList>
#include <QUrl>
#include <QVariant>
#include <algorithm>

#ifdef FLAKEWM_HAS_QGSETTINGS
#include <QGSettings/QGSettings>
#endif

#include "src/view/multitasking/multitasking_wallpaper.h"
#include "src/view/ssd/ssd_buffer/ssd_buffer.h"

namespace flakewm {
namespace view {
namespace {

QString ExistingPath(const QString& uri) {
  if (uri.isEmpty()) return {};
  const QUrl url(uri);
  const QString path = url.isLocalFile() ? url.toLocalFile() : uri;
  return QFileInfo::exists(path) ? QFileInfo(path).canonicalFilePath()
                                 : QString{};
}

QString WallpaperPath() {
#ifdef FLAKEWM_HAS_QGSETTINGS
  constexpr auto kAppearance = "com.deepin.dde.appearance";
  if (QGSettings::isSchemaInstalled(kAppearance)) {
    QGSettings settings(kAppearance);
    const QVariant value = settings.get(QStringLiteral("backgroundUris"));
    const QStringList uris = value.toStringList();
    if (!uris.isEmpty()) {
      const QString path = ExistingPath(uris.front());
      if (!path.isEmpty()) return path;
    }
  }
  constexpr auto kGnomeWrap = "com.deepin.wrap.gnome.desktop.background";
  if (QGSettings::isSchemaInstalled(kGnomeWrap)) {
    QGSettings settings(kGnomeWrap);
    const QString path =
        ExistingPath(settings.get(QStringLiteral("pictureUri")).toString());
    if (!path.isEmpty()) return path;
  }
#endif
  const QStringList fallbacks = {
      QStringLiteral("/usr/share/backgrounds/default_background.jpg"),
      QStringLiteral("/usr/share/wallpapers/deepin/desktop.jpg"),
  };
  for (const QString& path : fallbacks) {
    if (QFileInfo::exists(path)) return path;
  }
  return {};
}

}  // namespace

SsdBuffer* CreateMultitaskingWallpaper(int width, int height) {
  if (width <= 0 || height <= 0) return nullptr;
  SsdBuffer* buffer = SsdBuffer::Create(width, height);
  if (buffer == nullptr) return nullptr;

  QImage& destination = buffer->Image();
  destination.fill(Qt::black);
  QImageReader reader(WallpaperPath());
  reader.setAutoTransform(true);
  QImage wallpaper = reader.read();
  if (!wallpaper.isNull()) {
    wallpaper = wallpaper.scaled(width, height, Qt::KeepAspectRatioByExpanding,
                                 Qt::SmoothTransformation);
    const int x = std::max(0, (wallpaper.width() - width) / 2);
    const int y = std::max(0, (wallpaper.height() - height) / 2);
    wallpaper = wallpaper.copy(x, y, width, height);
    QPainter painter(&destination);
    painter.drawImage(0, 0, wallpaper);
  }
  return buffer;
}

SsdBuffer* CreateMultitaskingWorkspaceWallpaper(SsdBuffer* wallpaper, int width,
                                                int height) {
  if (wallpaper == nullptr || width <= 0 || height <= 0) return nullptr;
  SsdBuffer* buffer = SsdBuffer::Create(width, height);
  if (buffer == nullptr) return nullptr;
  QImage& destination = buffer->Image();
  destination.fill(Qt::transparent);
  QPainter painter(&destination);
  painter.setRenderHint(QPainter::Antialiasing, true);
  QPainterPath clip;
  clip.addRoundedRect(QRectF(0, 0, width, height), 7, 7);
  painter.setClipPath(clip);
  painter.drawImage(destination.rect(), wallpaper->Image());
  return buffer;
}

}  // namespace view
}  // namespace flakewm
