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

#ifndef SRC_PROTOCOL_INPUT_TIMESTAMPS_INPUT_TIMESTAMPS_MANAGER_INPUT_TIMESTAMPS_MANAGER_H_
#define SRC_PROTOCOL_INPUT_TIMESTAMPS_INPUT_TIMESTAMPS_MANAGER_INPUT_TIMESTAMPS_MANAGER_H_

#include <cstdint>
#include <list>

#include "protocol/input-timestamps-unstable-v1-protocol.h"
#include "src/wlr_wrapper/wlroots.h"

namespace flakewm {
namespace protocol {

class InputTimestampsManager final {
 public:
  explicit InputTimestampsManager(wl_display* display);
  ~InputTimestampsManager();

  InputTimestampsManager(const InputTimestampsManager&) = delete;
  InputTimestampsManager& operator=(const InputTimestampsManager&) = delete;

  bool IsValid() const;
  void SendKeyboard(wl_client* client, uint32_t time_msec) const;
  void SendPointer(wl_client* client, uint32_t time_msec) const;
  void SendTouch(wl_client* client, uint32_t time_msec) const;

 private:
  enum class Device { kKeyboard, kPointer, kTouch };

  struct Subscription {
    wl_listener target_destroy = {};
    InputTimestampsManager* manager = nullptr;
    wl_resource* resource = nullptr;
    wl_resource* target = nullptr;
    Device device = Device::kPointer;
  };

  static void Bind(wl_client* client, void* data, uint32_t version,
                   uint32_t id);
  static void DestroyManager(wl_client* client, wl_resource* resource);
  static void GetKeyboard(wl_client* client, wl_resource* resource, uint32_t id,
                          wl_resource* keyboard);
  static void GetPointer(wl_client* client, wl_resource* resource, uint32_t id,
                         wl_resource* pointer);
  static void GetTouch(wl_client* client, wl_resource* resource, uint32_t id,
                       wl_resource* touch);
  static void DestroyTimestamp(wl_client* client, wl_resource* resource);
  static void OnTimestampDestroyed(wl_resource* resource);
  static void OnTargetDestroyed(wl_listener* listener, void* data);

  void CreateSubscription(wl_client* client, uint32_t id, wl_resource* target,
                          Device device);
  void RemoveSubscription(Subscription* subscription);
  void Send(Device device, wl_client* client, uint32_t time_msec) const;

  static const struct zwp_input_timestamps_manager_v1_interface
      kManagerImplementation;
  static const struct zwp_input_timestamps_v1_interface
      kTimestampImplementation;

  wl_global* global_ = nullptr;
  std::list<Subscription*> subscriptions_;
};

}  // namespace protocol
}  // namespace flakewm

#endif  // SRC_PROTOCOL_INPUT_TIMESTAMPS_INPUT_TIMESTAMPS_MANAGER_INPUT_TIMESTAMPS_MANAGER_H_
