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
 */

#include "src/protocol/treeland/treeland_color_scheme.h"

#include <absl/log/absl_log.h>

#include <QObject>
#include <QString>
#include <QVariant>
#include <memory>
#include <utility>

#ifdef FLAKEWM_HAS_QGSETTINGS
#include <QGSettings/QGSettings>
#endif

#include "protocol/treeland-personalization-manager-v1-protocol.h"

namespace flakewm {
namespace protocol {
namespace {

constexpr char kGnomeSchema[] = "org.gnome.desktop.interface";
constexpr char kGnomeKey[] = "colorScheme";
constexpr char kDeepinSchema[] = "com.deepin.dde.appearance";
constexpr char kDeepinKey[] = "gtkTheme";

}  // namespace

TreelandColorScheme::TreelandColorScheme(ThemeTypeHandler handler)
    : handler_(std::move(handler)) {}

TreelandColorScheme::~TreelandColorScheme() = default;

void TreelandColorScheme::Start() {
#ifdef FLAKEWM_HAS_QGSETTINGS
  if (QGSettings::isSchemaInstalled(kGnomeSchema)) {
    gnome_settings_ = std::make_unique<QGSettings>(kGnomeSchema);
    if (gnome_settings_->keys().contains(QString::fromLatin1(kGnomeKey))) {
      QObject::connect(gnome_settings_.get(), &QGSettings::changed,
                       [this](const QString& key) {
                         if (key != QLatin1String(kGnomeKey)) return;
                         UpdateScheme(&gnome_scheme_, ReadGnomeScheme(),
                                      Source::kGnome);
                       });
    } else {
      gnome_settings_.reset();
    }
  }
  if (QGSettings::isSchemaInstalled(kDeepinSchema)) {
    deepin_settings_ = std::make_unique<QGSettings>(kDeepinSchema);
    QObject::connect(deepin_settings_.get(), &QGSettings::changed,
                     [this](const QString& key) {
                       if (key != QLatin1String(kDeepinKey)) return;
                       UpdateScheme(&deepin_scheme_, ReadDeepinScheme(),
                                    Source::kDeepin);
                     });
  }
#else
  ABSL_LOG(INFO) << "Color scheme: built without QGSettings, window theme "
                    "type is left to clients";
#endif

  gnome_scheme_ = ReadGnomeScheme();
  deepin_scheme_ = ReadDeepinScheme();
  last_source_ =
      gnome_scheme_ != Scheme::kNone ? Source::kGnome : Source::kDeepin;
  Apply();
}

TreelandColorScheme::Scheme TreelandColorScheme::ReadGnomeScheme() const {
#ifdef FLAKEWM_HAS_QGSETTINGS
  if (gnome_settings_ == nullptr) return Scheme::kNone;
  const QString value =
      gnome_settings_->get(QString::fromLatin1(kGnomeKey)).toString();
  if (value == QLatin1String("prefer-dark")) return Scheme::kDark;
  if (value == QLatin1String("prefer-light")) return Scheme::kLight;
#endif
  return Scheme::kNone;
}

TreelandColorScheme::Scheme TreelandColorScheme::ReadDeepinScheme() const {
#ifdef FLAKEWM_HAS_QGSETTINGS
  if (deepin_settings_ == nullptr) return Scheme::kNone;
  const QString value =
      deepin_settings_->get(QString::fromLatin1(kDeepinKey)).toString();
  if (value.isEmpty()) return Scheme::kNone;
  return value.contains(QLatin1String("dark"), Qt::CaseInsensitive)
             ? Scheme::kDark
             : Scheme::kLight;
#else
  return Scheme::kNone;
#endif
}

TreelandColorScheme::Scheme TreelandColorScheme::CurrentScheme() const {
  const Scheme last =
      last_source_ == Source::kGnome ? gnome_scheme_ : deepin_scheme_;
  if (last != Scheme::kNone) return last;
  if (gnome_scheme_ != Scheme::kNone) return gnome_scheme_;
  return deepin_scheme_;
}

void TreelandColorScheme::UpdateScheme(Scheme* scheme, Scheme value,
                                       Source source) {
  if (*scheme == value) return;
  *scheme = value;
  last_source_ = source;
  Apply();
}

void TreelandColorScheme::Apply() {
  uint32_t theme_type =
      TREELAND_PERSONALIZATION_APPEARANCE_CONTEXT_V1_THEME_TYPE_AUTO;
  switch (CurrentScheme()) {
    case Scheme::kDark:
      theme_type =
          TREELAND_PERSONALIZATION_APPEARANCE_CONTEXT_V1_THEME_TYPE_DARK;
      break;
    case Scheme::kLight:
      theme_type =
          TREELAND_PERSONALIZATION_APPEARANCE_CONTEXT_V1_THEME_TYPE_LIGHT;
      break;
    case Scheme::kNone:
      break;
  }
  ABSL_LOG(INFO) << "Color scheme: window theme type " << theme_type;
  handler_(theme_type);
}

}  // namespace protocol
}  // namespace flakewm
