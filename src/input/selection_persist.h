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
 * Originally copyright by (C) 2026 GXDE Team.
 * Original license: GPL-3.0-or-later, see GXDE Wayland Compositor.
 * Redistributed with GPL-3.0-or-later.
 * Original code is modified to adapt C++ and Wlroots 0.20.2.
 */

#ifndef SRC_INPUT_SELECTION_PERSIST_H_
#define SRC_INPUT_SELECTION_PERSIST_H_

#include "src/utils/signal_listener.h"
#include "src/wlr_wrapper/wlroots.h"

namespace flakewm {
namespace input {

class SelectionPersist final {
 public:
  SelectionPersist(wl_display* display, wlr_seat* seat);
  ~SelectionPersist();

  SelectionPersist(const SelectionPersist&) = delete;
  SelectionPersist& operator=(const SelectionPersist&) = delete;

  struct Selection;
  // Called when a copy is destroyed, whoever destroys it.
  void ForgetSelection(const Selection* selection);

 private:
  static void OnSetSelection(SelectionPersist* persist, void*);
  static void Install(void* data);

  wl_display* display_;
  wl_event_loop* loop_;
  wlr_seat* seat_;
  Selection* cache_ = nullptr;
  utils::SignalListener<SelectionPersist, void> set_selection_{
    this,
    OnSetSelection
  };
};

}  // namespace input
}  // namespace flakewm

#endif  // SRC_INPUT_SELECTION_PERSIST_H_
