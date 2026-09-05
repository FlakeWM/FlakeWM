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

#include <absl/log/absl_log.h>
#include <libudev.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "src/backend/fbdev/fbdev_backend/fbdev_backend.h"
#include "src/backend/backend/backend.h"

namespace flakewm {
namespace backend {
namespace {

constexpr std::chrono::seconds kSessionTimeout(10);

bool EnvironmentEquals(const char* name, const char* value) {
  const char* current = std::getenv(name);
  return current != nullptr && std::strcmp(current, value) == 0;
}

bool HasNestedDisplay() {
  return std::getenv("WAYLAND_DISPLAY") != nullptr ||
         std::getenv("WAYLAND_SOCKET") != nullptr ||
         std::getenv("DISPLAY") != nullptr;
}

std::vector<std::string> ParseDevices(const char* devices) {
  std::vector<std::string> result;
  const char* start = devices;
  while (start != nullptr && *start != '\0') {
    const char* separator = std::strchr(start, ':');
    const std::size_t length =
        separator == nullptr ? std::strlen(start) : separator - start;
    if (length != 0) {
      result.emplace_back(start, length);
    }
    start = separator == nullptr ? nullptr : separator + 1;
  }
  return result;
}

std::vector<std::string> FindFramebufferDevices(wlr_session* session) {
  if (const char* devices = std::getenv("FLAKEWM_FB_DEVICES");
      devices != nullptr) {
    return ParseDevices(devices);
  }

  std::vector<std::string> result;
  udev_enumerate* enumerate = udev_enumerate_new(session->udev);
  if (enumerate == nullptr) {
    return result;
  }

  udev_enumerate_add_match_sysname(enumerate, "fb[0-9]*");
  udev_enumerate_add_match_subsystem(enumerate, "graphics");
  if (udev_enumerate_scan_devices(enumerate) < 0) {
    udev_enumerate_unref(enumerate);
    return result;
  }

  udev_list_entry* entry = nullptr;
  udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(enumerate)) {
    const char* path = udev_list_entry_get_name(entry);
    udev_device* device = udev_device_new_from_syspath(session->udev, path);
    if (device == nullptr) {
      continue;
    }

    const char* seat = udev_device_get_property_value(device, "ID_SEAT");
    if (seat == nullptr) {
      seat = "seat0";
    }
    const char* node = udev_device_get_devnode(device);
    if (node != nullptr &&
        (session->seat[0] == '\0' || std::strcmp(session->seat, seat) == 0)) {
      result.emplace_back(node);
    }
    udev_device_unref(device);
  }

  udev_enumerate_unref(enumerate);
  return result;
}

bool WaitForSession(wlr_session* session, wl_event_loop* event_loop) {
  if (session->active) {
    return true;
  }

  ABSL_LOG(INFO) << "Waiting for the fbdev session to become active";
  const auto deadline = std::chrono::steady_clock::now() + kSessionTimeout;
  while (!session->active) {
    const auto remaining = deadline - std::chrono::steady_clock::now();
    if (remaining <= std::chrono::steady_clock::duration::zero()) {
      break;
    }
    const int timeout = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(remaining)
            .count());
    if (wl_event_loop_dispatch(event_loop, timeout) < 0) {
      return false;
    }
  }
  return session->active;
}

}  // namespace

Backend::Backend(wl_event_loop* event_loop) { event_loop_ = event_loop; }

Backend::~Backend() { Destroy(); }

std::unique_ptr<Backend> Backend::Create(wl_event_loop* event_loop) {
  std::unique_ptr<Backend> backend(new Backend(event_loop));
  if (!backend->Initialize()) {
    return nullptr;
  }
  return backend;
}

bool Backend::Initialize() {
  if (EnvironmentEquals("FLAKEWM_BACKEND", "fbdev") ||
      EnvironmentEquals("WLR_BACKENDS", "fbdev")) {
    return InitializeFbdev();
  }

  handle_ = wlr_backend_autocreate(event_loop_, &session_);
  if (handle_ != nullptr) {
    return true;
  }

  if (HasNestedDisplay() || std::getenv("WLR_BACKENDS") != nullptr) {
    return false;
  }

  ABSL_LOG(WARNING) << "No usable DRM backend, trying fbdev";
  return InitializeFbdev();
}

bool Backend::InitializeFbdev() {
  session_ = wlr_session_create(event_loop_);
  if (session_ == nullptr || !WaitForSession(session_, event_loop_)) {
    ABSL_LOG(ERROR) << "Failed to start an fbdev session";
    Destroy();
    return false;
  }

  const std::vector<std::string> devices = FindFramebufferDevices(session_);
  if (devices.empty()) {
    ABSL_LOG(ERROR) << "No framebuffer devices found";
    Destroy();
    return false;
  }

  wlr_backend* multi = wlr_multi_backend_create(event_loop_);
  if (multi == nullptr) {
    Destroy();
    return false;
  }

  wlr_backend* framebuffer =
      fbdev::FbdevBackend::Create(event_loop_, session_, devices);
  if (framebuffer == nullptr || !wlr_multi_backend_add(multi, framebuffer)) {
    if (framebuffer != nullptr) {
      wlr_backend_destroy(framebuffer);
    }
    wlr_backend_destroy(multi);
    Destroy();
    return false;
  }

  wlr_backend* input = wlr_libinput_backend_create(session_);
  if (input == nullptr) {
    if (!EnvironmentEquals("WLR_LIBINPUT_NO_DEVICES", "1")) {
      ABSL_LOG(ERROR) << "Failed to create the libinput backend";
      wlr_backend_destroy(multi);
      Destroy();
      return false;
    }
  } else if (!wlr_multi_backend_add(multi, input)) {
    wlr_backend_destroy(input);
    wlr_backend_destroy(multi);
    Destroy();
    return false;
  }

  handle_ = multi;
  fbdev_ = true;
  ABSL_LOG(INFO) << "Using fbdev backend";
  return true;
}

void Backend::Destroy() {
  if (handle_ != nullptr) {
    wlr_backend_destroy(handle_);
    handle_ = nullptr;
  }
  if (session_ != nullptr) {
    wlr_session_destroy(session_);
    session_ = nullptr;
  }
  fbdev_ = false;
}

wlr_backend* Backend::Handle() const { return handle_; }

bool Backend::IsFbdev() const { return fbdev_; }

}  // namespace backend
}  // namespace flakewm
