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

#include <algorithm>
#include <cctype>
#include <string_view>
#include <utility>

#include "src/input/key_binding_manager.h"

namespace flakewm {
namespace input {
namespace {

std::string_view Trim(std::string_view value) {
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.front())) != 0) {
    value.remove_prefix(1);
  }
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.back())) != 0) {
    value.remove_suffix(1);
  }
  return value;
}

bool EqualsIgnoringCase(std::string_view left, std::string_view right) {
  return left.size() == right.size() &&
         std::equal(left.begin(), left.end(), right.begin(),
                    [](char lhs, char rhs) {
                      return std::tolower(static_cast<unsigned char>(lhs)) ==
                             std::tolower(static_cast<unsigned char>(rhs));
                    });
}

std::optional<uint32_t> ModifierForName(std::string_view name) {
  struct NamedModifier {
    std::string_view name;
    uint32_t modifier;
  };
  static constexpr std::array<NamedModifier, 17> kModifiers = {{
      {"shift", WLR_MODIFIER_SHIFT},
      {"caps", WLR_MODIFIER_CAPS},
      {"lock", WLR_MODIFIER_CAPS},
      {"ctrl", WLR_MODIFIER_CTRL},
      {"control", WLR_MODIFIER_CTRL},
      {"alt", WLR_MODIFIER_ALT},
      {"mod1", WLR_MODIFIER_ALT},
      {"num", WLR_MODIFIER_MOD2},
      {"mod2", WLR_MODIFIER_MOD2},
      {"mod3", WLR_MODIFIER_MOD3},
      {"win", WLR_MODIFIER_LOGO},
      {"logo", WLR_MODIFIER_LOGO},
      {"super", WLR_MODIFIER_LOGO},
      {"meta", WLR_MODIFIER_LOGO},
      {"mod4", WLR_MODIFIER_LOGO},
      {"mod5", WLR_MODIFIER_MOD5},
      {"iso_level3_shift", WLR_MODIFIER_MOD5},
  }};
  for (const NamedModifier& modifier : kModifiers) {
    if (EqualsIgnoringCase(name, modifier.name)) {
      return modifier.modifier;
    }
  }
  return std::nullopt;
}

}  // namespace

std::optional<KeyBindingManager::BindingId> KeyBindingManager::Register(
    std::string shortcut, KeyBindingType type, Action action,
    std::string description) {
  std::optional<ParsedShortcut> parsed = ParseShortcut(shortcut);
  if (!parsed.has_value() || !action ||
      static_cast<std::size_t>(type) >= type_block_counts_.size()) {
    return std::nullopt;
  }
  const bool duplicate = std::any_of(
      bindings_.begin(), bindings_.end(), [&](const Binding& binding) {
        return binding.parsed.keysym == parsed->keysym &&
               binding.parsed.modifiers == parsed->modifiers;
      });
  if (duplicate) {
    return std::nullopt;
  }

  const BindingId id = next_id_++;
  bindings_.push_back({
      .id = id,
      .parsed = *parsed,
      .type = type,
      .action = std::move(action),
      .shortcut = std::move(shortcut),
      .description = std::move(description),
  });
  return id;
}

bool KeyBindingManager::Update(BindingId id, std::string shortcut,
                               std::string description) {
  const std::optional<ParsedShortcut> parsed = ParseShortcut(shortcut);
  if (!parsed.has_value()) {
    return false;
  }
  auto binding =
      std::find_if(bindings_.begin(), bindings_.end(),
                   [id](const Binding& item) { return item.id == id; });
  if (binding == bindings_.end()) {
    return false;
  }
  const bool duplicate =
      std::any_of(bindings_.begin(), bindings_.end(), [&](const Binding& item) {
        return item.id != id && item.parsed.keysym == parsed->keysym &&
               item.parsed.modifiers == parsed->modifiers;
      });
  if (duplicate) {
    return false;
  }

  binding->parsed = *parsed;
  binding->shortcut = std::move(shortcut);
  binding->description = std::move(description);
  return true;
}

bool KeyBindingManager::Unregister(BindingId id) {
  const auto old_size = bindings_.size();
  std::erase_if(bindings_,
                [id](const Binding& binding) { return binding.id == id; });
  return bindings_.size() != old_size;
}

std::vector<KeyBindingManager::BindingInfo> KeyBindingManager::Bindings()
    const {
  std::vector<BindingInfo> bindings;
  bindings.reserve(bindings_.size());
  for (const Binding& binding : bindings_) {
    bindings.push_back({
        .id = binding.id,
        .type = binding.type,
        .shortcut = binding.shortcut,
        .description = binding.description,
    });
  }
  return bindings;
}

const char* KeyBindingManager::TypeName(KeyBindingType type) {
  switch (type) {
    case KeyBindingType::kCustom:
      return "WLCOM_CUSTOM_DEF";
    case KeyBindingType::kWindowSwitch:
      return "WLCOM_WINDOW_SWITCHER";
    case KeyBindingType::kWindowMinimize:
      return "WLCOM_WINDOW_ACTION_MINIMIZE";
    case KeyBindingType::kWindowMaximize:
      return "WLCOM_WINDOW_ACTION_MAXIMIZE";
    case KeyBindingType::kWindowClose:
      return "WLCOM_WINDOW_ACTION_CLOSE";
    case KeyBindingType::kCount:
      return nullptr;
  }
  return nullptr;
}

void KeyBindingManager::GrabNextKey(CaptureCallback callback) {
  capture_callback_ = std::move(callback);
  captured_keyboard_ = nullptr;
  captured_keycode_ = 0;
  captured_shortcut_.clear();
}

void KeyBindingManager::CancelGrab() {
  capture_callback_ = {};
  captured_keyboard_ = nullptr;
  captured_keycode_ = 0;
  captured_shortcut_.clear();
}

bool KeyBindingManager::IsGrabbingKey() const {
  return static_cast<bool>(capture_callback_);
}

void KeyBindingManager::BlockAll(bool blocked) {
  if (blocked) {
    ++block_count_;
  } else if (block_count_ > 0) {
    --block_count_;
  }
}

void KeyBindingManager::BlockType(KeyBindingType type, bool blocked) {
  const std::size_t index = static_cast<std::size_t>(type);
  if (index >= type_block_counts_.size()) {
    return;
  }
  if (blocked) {
    ++type_block_counts_[index];
  } else if (type_block_counts_[index] > 0) {
    --type_block_counts_[index];
  }
}

bool KeyBindingManager::HandleKey(wlr_keyboard* keyboard,
                                  const wlr_keyboard_key_event& event,
                                  bool inhibited) {
  if (keyboard == nullptr || keyboard->xkb_state == nullptr) {
    return false;
  }

  auto active = std::find_if(
      active_keys_.begin(), active_keys_.end(), [&](const ActiveKey& key) {
        return key.keyboard == keyboard && key.keycode == event.keycode;
      });
  if (event.state == WL_KEYBOARD_KEY_STATE_RELEASED) {
    if (active != active_keys_.end()) {
      active_keys_.erase(active);
      return true;
    }
    if (IsGrabbingKey()) {
      return HandleCapturedKey(keyboard, event);
    }
    return false;
  }
  if (event.state != WL_KEYBOARD_KEY_STATE_PRESSED) {
    return false;
  }

  if (IsGrabbingKey()) {
    return HandleCapturedKey(keyboard, event);
  }

  if (active != active_keys_.end()) {
    const auto binding = std::find_if(bindings_.begin(), bindings_.end(),
                                      [active](const Binding& item) {
                                        return item.id == active->binding_id;
                                      });
    if (binding != bindings_.end() && !binding->parsed.no_repeat &&
        !inhibited && block_count_ == 0 && !IsTypeBlocked(binding->type)) {
      binding->action();
    }
    return true;
  }
  if (inhibited || block_count_ > 0) {
    return false;
  }

  const xkb_keysym_t* keysyms = nullptr;
  const int keysym_count =
      xkb_state_key_get_syms(keyboard->xkb_state, event.keycode + 8, &keysyms);
  if (keysym_count <= 0 || keysyms == nullptr) {
    return false;
  }
  const uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard);
  for (int index = 0; index < keysym_count; ++index) {
    const xkb_keysym_t keysym = keysyms[index] == XKB_KEY_ISO_Left_Tab
                                    ? XKB_KEY_Tab
                                    : xkb_keysym_to_lower(keysyms[index]);
    auto binding = std::find_if(
        bindings_.begin(), bindings_.end(), [&](const Binding& item) {
          return !IsTypeBlocked(item.type) && item.parsed.keysym == keysym &&
                 ModifiersMatch(modifiers, item.parsed.modifiers);
        });
    if (binding == bindings_.end()) {
      continue;
    }

    const BindingId binding_id = binding->id;
    const Action action = binding->action;
    action();
    active_keys_.push_back({
        .keyboard = keyboard,
        .keycode = event.keycode,
        .binding_id = binding_id,
    });
    return true;
  }
  return false;
}

void KeyBindingManager::ForgetKeyboard(wlr_keyboard* keyboard) {
  std::erase_if(active_keys_, [keyboard](const ActiveKey& key) {
    return key.keyboard == keyboard;
  });
  if (captured_keyboard_ == keyboard) {
    CancelGrab();
  }
}

std::optional<KeyBindingManager::ParsedShortcut>
KeyBindingManager::ParseShortcut(const std::string& shortcut) {
  std::string_view binding = Trim(shortcut);
  bool no_repeat = false;
  const std::size_t suffix = binding.rfind(':');
  if (suffix != std::string_view::npos) {
    if (!EqualsIgnoringCase(Trim(binding.substr(suffix + 1)), "no")) {
      return std::nullopt;
    }
    no_repeat = true;
    binding = Trim(binding.substr(0, suffix));
  }
  if (binding.empty()) {
    return std::nullopt;
  }

  ParsedShortcut parsed = {.no_repeat = no_repeat};
  std::size_t start = 0;
  while (start <= binding.size()) {
    const std::size_t separator = binding.find('+', start);
    const std::string_view part =
        Trim(binding.substr(start, separator - start));
    if (part.empty()) {
      return std::nullopt;
    }
    if (const std::optional<uint32_t> modifier = ModifierForName(part);
        modifier.has_value()) {
      if ((parsed.modifiers & *modifier) != 0) {
        return std::nullopt;
      }
      parsed.modifiers |= *modifier;
    } else {
      if (parsed.keysym != XKB_KEY_NoSymbol) {
        return std::nullopt;
      }
      const std::string key_name(part);
      parsed.keysym = xkb_keysym_to_lower(
          xkb_keysym_from_name(key_name.c_str(), XKB_KEYSYM_CASE_INSENSITIVE));
      if (parsed.keysym == XKB_KEY_NoSymbol) {
        return std::nullopt;
      }
    }
    if (separator == std::string_view::npos) {
      break;
    }
    start = separator + 1;
  }
  if (parsed.keysym == XKB_KEY_NoSymbol) {
    return std::nullopt;
  }
  return parsed;
}

bool KeyBindingManager::IsModifierKeysym(xkb_keysym_t keysym) {
  switch (keysym) {
    case XKB_KEY_Shift_L:
    case XKB_KEY_Shift_R:
    case XKB_KEY_Control_L:
    case XKB_KEY_Control_R:
    case XKB_KEY_Alt_L:
    case XKB_KEY_Alt_R:
    case XKB_KEY_Super_L:
    case XKB_KEY_Super_R:
    case XKB_KEY_Meta_L:
    case XKB_KEY_Meta_R:
    case XKB_KEY_Caps_Lock:
    case XKB_KEY_Num_Lock:
      return true;
    default:
      return false;
  }
}

std::string KeyBindingManager::BuildShortcut(
    const ParsedShortcut& captured_shortcut) {
  struct ModifierName {
    uint32_t modifier;
    std::string_view name;
  };
  static constexpr std::array<ModifierName, 8> kModifiers = {{
      {WLR_MODIFIER_SHIFT, "Shift"},
      {WLR_MODIFIER_CAPS, "Caps"},
      {WLR_MODIFIER_CTRL, "Ctrl"},
      {WLR_MODIFIER_ALT, "Alt"},
      {WLR_MODIFIER_MOD2, "Mod2"},
      {WLR_MODIFIER_MOD3, "Mod3"},
      {WLR_MODIFIER_LOGO, "Win"},
      {WLR_MODIFIER_MOD5, "Mod5"},
  }};

  std::string shortcut;
  for (const ModifierName& modifier : kModifiers) {
    if ((captured_shortcut.modifiers & modifier.modifier) == 0) {
      continue;
    }
    if (!shortcut.empty()) {
      shortcut += '+';
    }
    shortcut += modifier.name;
  }

  std::array<char, 64> keysym_name = {};
  if (xkb_keysym_get_name(xkb_keysym_to_lower(captured_shortcut.keysym),
                          keysym_name.data(), keysym_name.size()) <= 0) {
    return {};
  }
  if (!shortcut.empty()) {
    shortcut += '+';
  }
  shortcut += keysym_name.data();
  return shortcut;
}

bool KeyBindingManager::HandleCapturedKey(wlr_keyboard* keyboard,
                                          const wlr_keyboard_key_event& event) {
  const xkb_keysym_t* keysyms = nullptr;
  const int keysym_count =
      xkb_state_key_get_syms(keyboard->xkb_state, event.keycode + 8, &keysyms);
  if (keysym_count <= 0 || keysyms == nullptr) {
    return true;
  }
  const xkb_keysym_t keysym = xkb_keysym_to_lower(keysyms[0]);

  if (event.state == WL_KEYBOARD_KEY_STATE_PRESSED) {
    if (IsModifierKeysym(keysym) || captured_keyboard_ != nullptr) {
      return true;
    }
    std::string shortcut = BuildShortcut({
        .modifiers = wlr_keyboard_get_modifiers(keyboard),
        .keysym = keysym,
    });
    if (shortcut.empty()) {
      return true;
    }
    captured_keyboard_ = keyboard;
    captured_keycode_ = event.keycode;
    captured_shortcut_ = std::move(shortcut);
    capture_callback_(true, captured_shortcut_);
    return true;
  }

  if (event.state == WL_KEYBOARD_KEY_STATE_RELEASED &&
      captured_keyboard_ == keyboard && captured_keycode_ == event.keycode) {
    CaptureCallback callback = capture_callback_;
    const std::string shortcut = captured_shortcut_;
    CancelGrab();
    callback(false, shortcut);
  }
  return true;
}

bool KeyBindingManager::ModifiersMatch(uint32_t active, uint32_t required) {
  constexpr uint32_t kLockModifiers = WLR_MODIFIER_CAPS | WLR_MODIFIER_MOD2;
  const uint32_t ignored = kLockModifiers & ~required;
  return (active & ~ignored) == required;
}

bool KeyBindingManager::IsTypeBlocked(KeyBindingType type) const {
  const std::size_t index = static_cast<std::size_t>(type);
  return index >= type_block_counts_.size() || type_block_counts_[index] > 0;
}

}  // namespace input
}  // namespace flakewm
