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
 * Wayfire's UNIX signal handler, which is in their main.cpp
 * Originally licensed under The MIT License.
 * Minimal modification(s) are applied to make the original function
 * compactiable w/ FlakeWM.
 */

#ifndef SRC_UTILS_SIGNAL_LISTENER_H_
#define SRC_UTILS_SIGNAL_LISTENER_H_

extern "C" {
#include <wayland-server-core.h>
}

namespace flakewm {
namespace utils {

template <typename Owner, typename Event>
class SignalListener {
 public:
  using Callback = void (*)(Owner* owner, Event* event);

  SignalListener(Owner* owner, Callback callback)
      : owner_(owner), callback_(callback) {
    listener_.notify = Notify;
    wl_list_init(&listener_.link);
  }

  ~SignalListener() { Disconnect(); }

  SignalListener(const SignalListener&) = delete;
  SignalListener& operator=(const SignalListener&) = delete;

  void Connect(wl_signal* signal) {
    Disconnect();
    wl_signal_add(signal, &listener_);
    connected_ = true;
  }

  void Disconnect() {
    if (!connected_) {
      return;
    }
    wl_list_remove(&listener_.link);
    wl_list_init(&listener_.link);
    connected_ = false;
  }

 private:
  static void Notify(wl_listener* listener, void* event_data) {
    auto* self = reinterpret_cast<SignalListener*>(listener);
    self->callback_(self->owner_, static_cast<Event*>(event_data));
  }

  // Keep this first so a wl_listener pointer is also a SignalListener pointer.
  wl_listener listener_ = {};
  Owner* owner_;
  Callback callback_;
  bool connected_ = false;
};

}  // namespace utils
}  // namespace flakewm

#endif  // SRC_UTILS_SIGNAL_LISTENER_H_
