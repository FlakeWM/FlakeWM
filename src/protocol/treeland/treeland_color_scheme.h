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

#ifndef SRC_PROTOCOL_TREELAND_TREELAND_COLOR_SCHEME_H_
#define SRC_PROTOCOL_TREELAND_TREELAND_COLOR_SCHEME_H_

#include <cstdint>
#include <functional>
#include <memory>

#ifdef FLAKEWM_HAS_QGSETTINGS
class QGSettings;
#endif

namespace flakewm {
namespace protocol {

// Follows the system dark/light preference and reports it as a
// treeland_personalization_appearance_context_v1 theme type, which DTK5/6
// Wayland clients map to the "deepin" / "deepin-dark" theme.
//
// Sources:
//  - org.gnome.desktop.interface color-scheme
//    (prefer-dark / prefer-light / default)
//  - com.deepin.dde.appearance gtk-theme (dark when the name contains "dark")
// The one changed most recently wins; at startup an explicit color-scheme
// wins over gtk-theme. The same rule is used by dtk2widget, gxde-wlcom and
// xdg-desktop-portal-gxde.
//
// Needs QGSettings; without it the theme type is left to clients.
class TreelandColorScheme final {
 public:
  // Called with a TREELAND_PERSONALIZATION_APPEARANCE_CONTEXT_V1_THEME_TYPE_*
  // value whenever the preference changes, and once from Start().
  using ThemeTypeHandler = std::function<void(uint32_t theme_type)>;

  explicit TreelandColorScheme(ThemeTypeHandler handler);
  ~TreelandColorScheme();

  TreelandColorScheme(const TreelandColorScheme&) = delete;
  TreelandColorScheme& operator=(const TreelandColorScheme&) = delete;

  void Start();

 private:
  enum class Scheme : uint8_t { kNone, kDark, kLight };
  enum class Source : uint8_t { kGnome, kDeepin };

  Scheme ReadGnomeScheme() const;
  Scheme ReadDeepinScheme() const;
  Scheme CurrentScheme() const;
  void UpdateScheme(Scheme* scheme, Scheme value, Source source);
  void Apply();

  ThemeTypeHandler handler_;
#ifdef FLAKEWM_HAS_QGSETTINGS
  std::unique_ptr<QGSettings> gnome_settings_;
  std::unique_ptr<QGSettings> deepin_settings_;
#endif
  Scheme gnome_scheme_ = Scheme::kNone;
  Scheme deepin_scheme_ = Scheme::kNone;
  Source last_source_ = Source::kGnome;
};

}  // namespace protocol
}  // namespace flakewm

#endif  // SRC_PROTOCOL_TREELAND_TREELAND_COLOR_SCHEME_H_
