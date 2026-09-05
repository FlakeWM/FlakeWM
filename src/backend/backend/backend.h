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

#ifndef SRC_BACKEND_BACKEND_BACKEND_H_
#define SRC_BACKEND_BACKEND_BACKEND_H_

#include <memory>

#include "src/wlr_wrapper/wlroots.h"

namespace flakewm {
namespace backend {

class Backend final {
 public:
  static std::unique_ptr<Backend> Create(wl_event_loop* event_loop);
  ~Backend();

  Backend(const Backend&) = delete;
  Backend& operator=(const Backend&) = delete;

  wlr_backend* Handle() const;
  bool IsFbdev() const;

 private:
  explicit Backend(wl_event_loop* event_loop);

  bool Initialize();
  bool InitializeFbdev();
  void Destroy();

  wl_event_loop* event_loop_ = nullptr;
  wlr_backend* handle_ = nullptr;
  wlr_session* session_ = nullptr;
  bool fbdev_ = false;
};

}  // namespace backend
}  // namespace flakewm

#endif  // SRC_BACKEND_BACKEND_BACKEND_H_
