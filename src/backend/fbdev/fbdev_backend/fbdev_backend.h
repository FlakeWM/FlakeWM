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
 * Original license: GPL-1.0-or-later, see Open Kylin Wayland Compositor.
 * Redistributed with GPL-3.0-or-later.
 * Original code is modified to adapt C++ and Wlroots 0.20.2.
 */

#ifndef SRC_BACKEND_FBDEV_FBDEV_BACKEND_FBDEV_BACKEND_H_
#define SRC_BACKEND_FBDEV_FBDEV_BACKEND_FBDEV_BACKEND_H_

#include <string>
#include <vector>

#include "src/backend/fbdev/fbdev_output/fbdev_output.h"
#include "src/utils/signal_listener.h"
#include "src/wlr_wrapper/wlroots.h"

namespace flakewm {
namespace backend {
namespace fbdev {

class FbdevBackend final {
 public:
  static wlr_backend* Create(wl_event_loop* event_loop, wlr_session* session,
                             const std::vector<std::string>& devices);

  FbdevBackend(const FbdevBackend&) = delete;
  FbdevBackend& operator=(const FbdevBackend&) = delete;

  wlr_backend* Handle();
  static bool IsHandle(const wlr_backend* backend);
  wl_event_loop* EventLoop() const;
  wlr_session* Session() const;
  void RemoveOutput(FbdevOutput* output);

 private:
  FbdevBackend(wl_event_loop* event_loop, wlr_session* session);
  ~FbdevBackend();

  bool Initialize(const std::vector<std::string>& devices);
  static FbdevBackend* FromHandle(wlr_backend* backend);
  static const wlr_backend_impl* Implementation();
  static bool Start(wlr_backend* backend);
  static void Destroy(wlr_backend* backend);
  static void OnSessionActive(FbdevBackend* backend, void*);
  static void OnSessionDestroy(FbdevBackend* backend, void*);

  wlr_backend handle_ = {};
  wl_event_loop* event_loop_ = nullptr;
  wlr_session* session_ = nullptr;
  std::vector<FbdevOutput*> outputs_;
  utils::SignalListener<FbdevBackend, void> session_active_{this,
                                                            OnSessionActive};
  utils::SignalListener<FbdevBackend, void> session_destroy_{this,
                                                             OnSessionDestroy};
};

bool IsFbdevBackend(const wlr_backend* backend);
bool IsFbdevOutput(const wlr_output* output);

}  // namespace fbdev
}  // namespace backend
}  // namespace flakewm

#endif  // SRC_BACKEND_FBDEV_FBDEV_BACKEND_FBDEV_BACKEND_H_
