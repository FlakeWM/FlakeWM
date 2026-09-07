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

#include <new>

#include "src/protocol/input_timestamps/input_timestamps_manager/input_timestamps_manager.h"

namespace flakewm {
namespace protocol {

const struct zwp_input_timestamps_manager_v1_interface
    InputTimestampsManager::kManagerImplementation = {
        .destroy = InputTimestampsManager::DestroyManager,
        .get_keyboard_timestamps = InputTimestampsManager::GetKeyboard,
        .get_pointer_timestamps = InputTimestampsManager::GetPointer,
        .get_touch_timestamps = InputTimestampsManager::GetTouch,
};

const struct zwp_input_timestamps_v1_interface
    InputTimestampsManager::kTimestampImplementation = {
        .destroy = InputTimestampsManager::DestroyTimestamp,
};

InputTimestampsManager::InputTimestampsManager(wl_display* display) {
  global_ = wl_global_create(
      display, &zwp_input_timestamps_manager_v1_interface, 1, this, Bind);
}

InputTimestampsManager::~InputTimestampsManager() {
  while (!subscriptions_.empty()) {
    Subscription* subscription = subscriptions_.front();
    wl_resource_set_user_data(subscription->resource, nullptr);
    RemoveSubscription(subscription);
  }
  if (global_ != nullptr) {
    wl_global_destroy(global_);
  }
}

bool InputTimestampsManager::IsValid() const { return global_ != nullptr; }

void InputTimestampsManager::SendKeyboard(wl_client* client,
                                          uint32_t time_msec) const {
  Send(Device::kKeyboard, client, time_msec);
}

void InputTimestampsManager::SendPointer(wl_client* client,
                                         uint32_t time_msec) const {
  Send(Device::kPointer, client, time_msec);
}

void InputTimestampsManager::SendTouch(wl_client* client,
                                       uint32_t time_msec) const {
  Send(Device::kTouch, client, time_msec);
}

void InputTimestampsManager::Bind(wl_client* client, void* data,
                                  uint32_t version, uint32_t id) {
  wl_resource* resource = wl_resource_create(
      client, &zwp_input_timestamps_manager_v1_interface, version, id);
  if (resource == nullptr) {
    wl_client_post_no_memory(client);
    return;
  }
  wl_resource_set_implementation(resource, &kManagerImplementation, data,
                                 nullptr);
}

void InputTimestampsManager::DestroyManager(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void InputTimestampsManager::GetKeyboard(wl_client* client,
                                         wl_resource* resource, uint32_t id,
                                         wl_resource* keyboard) {
  static_cast<InputTimestampsManager*>(wl_resource_get_user_data(resource))
      ->CreateSubscription(client, id, keyboard, Device::kKeyboard);
}

void InputTimestampsManager::GetPointer(wl_client* client,
                                        wl_resource* resource, uint32_t id,
                                        wl_resource* pointer) {
  static_cast<InputTimestampsManager*>(wl_resource_get_user_data(resource))
      ->CreateSubscription(client, id, pointer, Device::kPointer);
}

void InputTimestampsManager::GetTouch(wl_client* client, wl_resource* resource,
                                      uint32_t id, wl_resource* touch) {
  static_cast<InputTimestampsManager*>(wl_resource_get_user_data(resource))
      ->CreateSubscription(client, id, touch, Device::kTouch);
}

void InputTimestampsManager::DestroyTimestamp(wl_client*,
                                              wl_resource* resource) {
  wl_resource_destroy(resource);
}

void InputTimestampsManager::OnTimestampDestroyed(wl_resource* resource) {
  auto* subscription =
      static_cast<Subscription*>(wl_resource_get_user_data(resource));
  if (subscription != nullptr) {
    subscription->manager->RemoveSubscription(subscription);
  }
}

void InputTimestampsManager::OnTargetDestroyed(wl_listener* listener, void*) {
  // target_destroy is the first member, so the listener carries its owner.
  auto* subscription = reinterpret_cast<Subscription*>(listener);
  subscription->target = nullptr;
  wl_list_remove(&subscription->target_destroy.link);
  wl_list_init(&subscription->target_destroy.link);
}

void InputTimestampsManager::CreateSubscription(wl_client* client, uint32_t id,
                                                wl_resource* target,
                                                Device device) {
  if (target == nullptr || wl_resource_get_client(target) != client) {
    wl_client_post_implementation_error(client, "invalid input resource");
    return;
  }

  wl_resource* resource =
      wl_resource_create(client, &zwp_input_timestamps_v1_interface, 1, id);
  auto* subscription = new (std::nothrow) Subscription;
  if (resource == nullptr || subscription == nullptr) {
    delete subscription;
    if (resource != nullptr) {
      wl_resource_destroy(resource);
    }
    wl_client_post_no_memory(client);
    return;
  }

  subscription->manager = this;
  subscription->resource = resource;
  subscription->target = target;
  subscription->device = device;
  subscription->target_destroy.notify = OnTargetDestroyed;
  wl_resource_add_destroy_listener(target, &subscription->target_destroy);
  subscriptions_.push_back(subscription);
  wl_resource_set_implementation(resource, &kTimestampImplementation,
                                 subscription, OnTimestampDestroyed);
}

void InputTimestampsManager::RemoveSubscription(Subscription* subscription) {
  subscriptions_.remove(subscription);
  if (subscription->target != nullptr) {
    wl_list_remove(&subscription->target_destroy.link);
  }
  delete subscription;
}

void InputTimestampsManager::Send(Device device, wl_client* client,
                                  uint32_t time_msec) const {
  if (client == nullptr) {
    return;
  }

  const uint64_t seconds = time_msec / 1000;
  const uint32_t nanoseconds = (time_msec % 1000) * 1000000;
  for (const Subscription* subscription : subscriptions_) {
    if (subscription->device != device || subscription->target == nullptr ||
        wl_resource_get_client(subscription->target) != client) {
      continue;
    }
    zwp_input_timestamps_v1_send_timestamp(
        subscription->resource, static_cast<uint32_t>(seconds >> 32),
        static_cast<uint32_t>(seconds), nanoseconds);
  }
}

}  // namespace protocol
}  // namespace flakewm
