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

#include <algorithm>
#include <string>
#include <vector>

#include "src/backend/fbdev/fbdev_backend/fbdev_backend.h"

namespace flakewm {
namespace backend {
namespace fbdev {

FbdevBackend::FbdevBackend(wl_event_loop* event_loop, wlr_session* session) {
  event_loop_ = event_loop;
  session_ = session;
  wlr_backend_init(&handle_, Implementation());
  handle_.buffer_caps = WLR_BUFFER_CAP_DATA_PTR | WLR_BUFFER_CAP_SHM;
}

FbdevBackend::~FbdevBackend() = default;

wlr_backend* FbdevBackend::Create(wl_event_loop* event_loop,
                                  wlr_session* session,
                                  const std::vector<std::string>& devices) {
  auto* backend = new FbdevBackend(event_loop, session);
  if (!backend->Initialize(devices)) {
    wlr_backend_destroy(&backend->handle_);
    return nullptr;
  }
  return &backend->handle_;
}

bool FbdevBackend::Initialize(const std::vector<std::string>& devices) {
  for (std::size_t index = 0; index < devices.size(); ++index) {
    FbdevOutput* output =
        FbdevOutput::Create(this, devices[index], static_cast<int>(index));
    if (output != nullptr) {
      outputs_.push_back(output);
    }
  }

  if (outputs_.empty()) {
    return false;
  }

  session_active_.Connect(&session_->events.active);
  session_destroy_.Connect(&session_->events.destroy);
  return true;
}

wlr_backend* FbdevBackend::Handle() { return &handle_; }

wl_event_loop* FbdevBackend::EventLoop() const { return event_loop_; }

wlr_session* FbdevBackend::Session() const { return session_; }

void FbdevBackend::RemoveOutput(FbdevOutput* output) {
  const auto found = std::find(outputs_.begin(), outputs_.end(), output);
  if (found != outputs_.end()) {
    outputs_.erase(found);
  }
}

FbdevBackend* FbdevBackend::FromHandle(wlr_backend* backend) {
  return reinterpret_cast<FbdevBackend*>(backend);
}

const wlr_backend_impl* FbdevBackend::Implementation() {
  static const wlr_backend_impl implementation = {
      .start = Start,
      .destroy = Destroy,
  };
  return &implementation;
}

bool FbdevBackend::IsHandle(const wlr_backend* backend) {
  return backend != nullptr && backend->impl == Implementation();
}

bool FbdevBackend::Start(wlr_backend* backend_handle) {
  FbdevBackend* backend = FromHandle(backend_handle);
  for (FbdevOutput* output : backend->outputs_) {
    wl_signal_emit_mutable(&backend->handle_.events.new_output,
                           output->Handle());
  }
  return true;
}

void FbdevBackend::Destroy(wlr_backend* backend_handle) {
  if (backend_handle == nullptr) {
    return;
  }

  FbdevBackend* backend = FromHandle(backend_handle);
  backend->session_active_.Disconnect();
  backend->session_destroy_.Disconnect();
  wlr_backend_finish(backend_handle);

  while (!backend->outputs_.empty()) {
    wlr_output_destroy(backend->outputs_.back()->Handle());
  }
  delete backend;
}

void FbdevBackend::OnSessionActive(FbdevBackend* backend, void*) {
  for (FbdevOutput* output : backend->outputs_) {
    if (backend->session_->active) {
      if (!output->Reenable()) {
        ABSL_LOG(ERROR) << "Failed to restore fbdev output";
      }
    } else {
      output->Offscreen();
    }
  }
}

void FbdevBackend::OnSessionDestroy(FbdevBackend* backend, void*) {
  wlr_backend_destroy(&backend->handle_);
}

bool IsFbdevBackend(const wlr_backend* backend) {
  return FbdevBackend::IsHandle(backend);
}

bool IsFbdevOutput(const wlr_output* output) {
  return FbdevOutput::IsHandle(output);
}

}  // namespace fbdev
}  // namespace backend
}  // namespace flakewm
