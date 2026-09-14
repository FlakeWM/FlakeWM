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

#ifndef SRC_PROTOCOL_KDE_KDE_IDLE_MANAGER_H_
#define SRC_PROTOCOL_KDE_KDE_IDLE_MANAGER_H_

#include <vector>

#include "src/wlr_wrapper/wlroots.h"

namespace flakewm {
namespace protocol {

// Compatibility implementation of org_kde_kwin_idle.  wlroots already owns
// the modern ext-idle-notify implementation; this class mirrors activity into
// the older KDE timer objects still used by Plasma components.
class KdeIdleManager final {
 public:
  KdeIdleManager(wl_display* display, wlr_seat* seat);
  ~KdeIdleManager();

  KdeIdleManager(const KdeIdleManager&) = delete;
  KdeIdleManager& operator=(const KdeIdleManager&) = delete;

  bool IsValid() const;
  void NotifyActivity();

 private:
  struct Timeout;

  static void Bind(wl_client* client, void* data, uint32_t version,
                   uint32_t id);
  static void GetIdleTimeout(wl_client* client, wl_resource* resource,
                             uint32_t id, wl_resource* seat_resource,
                             uint32_t timeout_ms);
  static void Release(wl_client* client, wl_resource* resource);
  static void SimulateUserActivity(wl_client* client, wl_resource* resource);
  static void DestroyTimeout(wl_resource* resource);
  static int OnTimer(void* data);

  void Arm(Timeout* timeout);
  void Remove(Timeout* timeout);

  wl_display* display_;
  wlr_seat* seat_;
  wl_global* global_ = nullptr;
  std::vector<Timeout*> timeouts_;
};

}  // namespace protocol
}  // namespace flakewm

#endif  // SRC_PROTOCOL_KDE_KDE_IDLE_MANAGER_H_
