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
 * Originally copyright by (C) 2024 KylinSoft Co., Ltd.
 * Originally copyright by (C) 2026 GXDE Team.
 * Original license: GPL-3.0-or-later, see GXDE Wayland Compositor.
 * Redistributed with GPL-3.0-or-later.
 * Original code is modified to adapt C++ and Wlroots 0.20.2.
 */

#ifndef SRC_INPUT_KEY_BINDING_MANAGER_H_
#define SRC_INPUT_KEY_BINDING_MANAGER_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "src/wlr_wrapper/wlroots.h"

namespace flakewm {
namespace input {

enum class KeyBindingType : std::uint8_t {
  kCustom = 0,
  kWindowSwitch,
  kWindowMinimize,
  kWindowMaximize,
  kWindowClose,
  kCount,
};

class KeyBindingManager final {
 public:
  using BindingId = std::uint64_t;
  using Action = std::function<void()>;
  using CaptureCallback =
      std::function<void(bool pressed, const std::string& shortcut)>;

  struct BindingInfo {
    BindingId id;
    KeyBindingType type;
    std::string shortcut;
    std::string description;
  };

  KeyBindingManager() = default;
  ~KeyBindingManager() = default;

  KeyBindingManager(const KeyBindingManager&) = delete;
  KeyBindingManager& operator=(const KeyBindingManager&) = delete;

  std::optional<BindingId> Register(std::string shortcut, KeyBindingType type,
                                    Action action,
                                    std::string description = {});
  bool Update(BindingId id, std::string shortcut, std::string description = {});
  bool Unregister(BindingId id);
  std::vector<BindingInfo> Bindings() const;
  static const char* TypeName(KeyBindingType type);

  void GrabNextKey(CaptureCallback callback);
  void CancelGrab();
  bool IsGrabbingKey() const;

  void BlockAll(bool blocked);
  void BlockType(KeyBindingType type, bool blocked);

  bool HandleKey(wlr_keyboard* keyboard, const wlr_keyboard_key_event& event,
                 bool inhibited);
  void ForgetKeyboard(wlr_keyboard* keyboard);

 private:
  struct ParsedShortcut {
    uint32_t modifiers = 0;
    xkb_keysym_t keysym = XKB_KEY_NoSymbol;
    bool no_repeat = false;
  };

  struct Binding {
    BindingId id;
    ParsedShortcut parsed;
    KeyBindingType type;
    Action action;
    std::string shortcut;
    std::string description;
  };

  struct ActiveKey {
    wlr_keyboard* keyboard;
    uint32_t keycode;
    BindingId binding_id;
  };

  static std::optional<ParsedShortcut> ParseShortcut(
      const std::string& shortcut);
  static bool IsModifierKeysym(xkb_keysym_t keysym);
  static std::string BuildShortcut(const ParsedShortcut& shortcut);
  static bool ModifiersMatch(uint32_t active, uint32_t required);
  bool HandleCapturedKey(wlr_keyboard* keyboard,
                         const wlr_keyboard_key_event& event);
  bool IsTypeBlocked(KeyBindingType type) const;

  std::vector<Binding> bindings_;
  std::vector<ActiveKey> active_keys_;
  std::array<std::size_t, static_cast<std::size_t>(KeyBindingType::kCount)>
      type_block_counts_ = {};
  BindingId next_id_ = 1;
  std::size_t block_count_ = 0;
  CaptureCallback capture_callback_;
  wlr_keyboard* captured_keyboard_ = nullptr;
  uint32_t captured_keycode_ = 0;
  std::string captured_shortcut_;
};

}  // namespace input
}  // namespace flakewm

#endif  // SRC_INPUT_KEY_BINDING_MANAGER_H_
