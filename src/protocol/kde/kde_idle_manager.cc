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

#include "src/protocol/kde/kde_idle_manager.h"

#include <algorithm>
#include <limits>

#include "protocol/idle-protocol.h"

namespace flakewm {
namespace protocol {
namespace {

constexpr uint32_t kKdeIdleVersion = 1;

}  // namespace

struct KdeIdleManager::Timeout {
  KdeIdleManager* manager;
  wl_resource* resource;
  wl_event_source* timer = nullptr;
  uint32_t timeout_ms;
  bool idle = false;
};

KdeIdleManager::KdeIdleManager(wl_display* display, wlr_seat* seat)
    : display_(display), seat_(seat) {
  global_ = wl_global_create(display_, &org_kde_kwin_idle_interface,
                             kKdeIdleVersion, this, Bind);
}

KdeIdleManager::~KdeIdleManager() {
  if (global_ != nullptr) {
    wl_global_destroy(global_);
  }
}

bool KdeIdleManager::IsValid() const { return global_ != nullptr; }

void KdeIdleManager::NotifyActivity() {
  // Callbacks cannot erase timeouts here: Wayland events are queued, not
  // dispatched recursively, so the collection remains stable.
  for (Timeout* timeout : timeouts_) {
    if (timeout->idle) {
      timeout->idle = false;
      org_kde_kwin_idle_timeout_send_resumed(timeout->resource);
    }
    Arm(timeout);
  }
}

void KdeIdleManager::Bind(wl_client* client, void* data, uint32_t version,
                          uint32_t id) {
  static const struct org_kde_kwin_idle_interface implementation = {
      .get_idle_timeout = GetIdleTimeout,
  };
  auto* manager = static_cast<KdeIdleManager*>(data);
  wl_resource* resource =
      wl_resource_create(client, &org_kde_kwin_idle_interface,
                         std::min(version, kKdeIdleVersion), id);
  if (resource == nullptr) {
    wl_client_post_no_memory(client);
    return;
  }
  wl_resource_set_implementation(resource, &implementation, manager, nullptr);
}

void KdeIdleManager::GetIdleTimeout(wl_client* client, wl_resource* resource,
                                    uint32_t id, wl_resource* seat_resource,
                                    uint32_t timeout_ms) {
  static const struct org_kde_kwin_idle_timeout_interface implementation = {
      .release = Release,
      .simulate_user_activity = SimulateUserActivity,
  };
  auto* manager =
      static_cast<KdeIdleManager*>(wl_resource_get_user_data(resource));
  wl_resource* timeout_resource =
      wl_resource_create(client, &org_kde_kwin_idle_timeout_interface,
                         wl_resource_get_version(resource), id);
  if (timeout_resource == nullptr) {
    wl_resource_post_no_memory(resource);
    return;
  }

  wlr_seat_client* seat_client = wlr_seat_client_from_resource(seat_resource);
  if (seat_client == nullptr || seat_client->seat != manager->seat_) {
    // The KDE protocol defines no invalid-seat error.  Keep the newly-created
    // object inert, matching the modern wlroots idle-notify implementation.
    wl_resource_set_implementation(timeout_resource, &implementation, nullptr,
                                   nullptr);
    return;
  }

  auto* timeout = new Timeout{
      .manager = manager,
      .resource = timeout_resource,
      .timeout_ms = timeout_ms,
  };
  if (timeout_ms > 0) {
    timeout->timer = wl_event_loop_add_timer(
        wl_display_get_event_loop(manager->display_), OnTimer, timeout);
    if (timeout->timer == nullptr) {
      delete timeout;
      wl_resource_post_no_memory(resource);
      wl_resource_destroy(timeout_resource);
      return;
    }
  }
  manager->timeouts_.push_back(timeout);
  wl_resource_set_implementation(timeout_resource, &implementation, timeout,
                                 DestroyTimeout);
  manager->Arm(timeout);
}

void KdeIdleManager::Release(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void KdeIdleManager::SimulateUserActivity(wl_client*, wl_resource* resource) {
  auto* timeout = static_cast<Timeout*>(wl_resource_get_user_data(resource));
  if (timeout == nullptr) {
    return;
  }
  if (timeout->idle) {
    timeout->idle = false;
    org_kde_kwin_idle_timeout_send_resumed(resource);
  }
  timeout->manager->Arm(timeout);
}

void KdeIdleManager::DestroyTimeout(wl_resource* resource) {
  auto* timeout = static_cast<Timeout*>(wl_resource_get_user_data(resource));
  if (timeout == nullptr) {
    return;
  }
  timeout->manager->Remove(timeout);
}

int KdeIdleManager::OnTimer(void* data) {
  auto* timeout = static_cast<Timeout*>(data);
  if (!timeout->idle) {
    timeout->idle = true;
    org_kde_kwin_idle_timeout_send_idle(timeout->resource);
  }
  return 0;
}

void KdeIdleManager::Arm(Timeout* timeout) {
  if (timeout->timer != nullptr) {
    const uint32_t maximum_delay =
        static_cast<uint32_t>(std::numeric_limits<int>::max());
    const int delay =
        static_cast<int>(std::min(timeout->timeout_ms, maximum_delay));
    wl_event_source_timer_update(timeout->timer, delay);
  } else if (!timeout->idle) {
    timeout->idle = true;
    org_kde_kwin_idle_timeout_send_idle(timeout->resource);
  }
}

void KdeIdleManager::Remove(Timeout* timeout) {
  std::erase(timeouts_, timeout);
  if (timeout->timer != nullptr) {
    wl_event_source_remove(timeout->timer);
  }
  delete timeout;
}

}  // namespace protocol
}  // namespace flakewm
