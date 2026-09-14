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

#include <algorithm>
#include <memory>

#include "protocol/ukui-output-v1-protocol.h"
#include "src/core/compositor_private/compositor_private.h"
#include "src/protocol/ukui/ukui_protocol_manager_internal.h"

namespace flakewm {
namespace protocol {
namespace {
using ukui_internal::kOutputVersion;
using ukui_internal::Safe;
using ukui_internal::SameBox;
}  // namespace

void UkuiProtocolManager::Impl::AddOutput(wlr_output* handle) {
  if (handle == nullptr || FindOutput(handle) != nullptr) {
    return;
  }
  auto output = std::make_unique<Output>();
  output->manager = this;
  output->handle = handle;
  output->destroy.notify = OnOutputDestroy;
  wl_signal_add(&handle->events.destroy, &output->destroy);
  outputs.push_back(std::move(output));
  UpdateOutputs();
}

void UkuiProtocolManager::Impl::UpdateOutputs() {
  bool changed = false;
  for (const std::unique_ptr<Output>& output : outputs) {
    const bool enabled = output->handle != nullptr && output->handle->enabled;
    if (enabled && !output->announced) {
      output->announced = true;
      for (wl_resource* management : output_management_resources) {
        CreateOutputBinding(output.get(), management);
      }
      changed = true;
    } else if (!enabled && output->announced) {
      FinishOutput(output.get());
      output->announced = false;
      changed = true;
    }
    if (!enabled) {
      continue;
    }
    core::CompositorPrivate::Output* state =
        compositor->FindOutput(output->handle);
    wlr_box usable = state == nullptr ? wlr_box{} : state->usable_box;
    if (usable.width <= 0 || usable.height <= 0) {
      wlr_output_layout_get_box(output_layout, output->handle, &usable);
    }
    if (!SameBox(usable, output->last_usable)) {
      output->last_usable = usable;
      for (OutputBinding* binding : output->bindings) {
        ukui_output_v1_send_usable_area(binding->resource, usable.x, usable.y,
                                        usable.width, usable.height);
      }
      changed = true;
    }
  }
  if (changed) {
    for (wl_resource* resource : output_management_resources) {
      ukui_output_management_v1_send_done(resource);
    }
  }
}

void UkuiProtocolManager::Impl::BindOutputManagement(wl_client* client,
                                                     void* data,
                                                     uint32_t version,
                                                     uint32_t id) {
  static const struct ukui_output_management_v1_interface implementation = {
      .destroy = DestroyResourceRequest,
  };
  auto* manager = static_cast<Impl*>(data);
  wl_resource* resource = wl_resource_create(
      client, &ukui_output_management_v1_interface,
      static_cast<int>(std::min(version, kOutputVersion)), id);
  if (resource == nullptr) {
    wl_client_post_no_memory(client);
    return;
  }
  manager->output_management_resources.push_back(resource);
  wl_resource_set_implementation(resource, &implementation, manager,
                                 RemoveOutputManagement);
  for (const std::unique_ptr<Output>& output : manager->outputs) {
    if (output->announced) {
      manager->CreateOutputBinding(output.get(), resource);
    }
  }
  ukui_output_management_v1_send_done(resource);
}

void UkuiProtocolManager::Impl::RemoveOutputManagement(wl_resource* resource) {
  auto* manager = static_cast<Impl*>(wl_resource_get_user_data(resource));
  if (manager != nullptr) {
    std::erase(manager->output_management_resources, resource);
  }
}

void UkuiProtocolManager::Impl::CreateOutputBinding(Output* output,
                                                    wl_resource* management) {
  static const struct ukui_output_v1_interface implementation = {
      .destroy = DestroyResourceRequest,
  };
  wl_client* client = wl_resource_get_client(management);
  wl_resource* resource =
      wl_resource_create(client, &ukui_output_v1_interface,
                         wl_resource_get_version(management), 0);
  if (resource == nullptr) {
    wl_client_post_no_memory(client);
    return;
  }
  auto* binding = new OutputBinding{.output = output, .resource = resource};
  output->bindings.push_back(binding);
  wl_resource_set_implementation(resource, &implementation, binding,
                                 DestroyOutputBinding);
  ukui_output_management_v1_send_output(management, resource);
  ukui_output_v1_send_name(resource, Safe(output->handle->name));
  core::CompositorPrivate::Output* state =
      compositor->FindOutput(output->handle);
  wlr_box usable = state == nullptr ? wlr_box{} : state->usable_box;
  if (usable.width <= 0 || usable.height <= 0) {
    wlr_output_layout_get_box(output_layout, output->handle, &usable);
  }
  output->last_usable = usable;
  ukui_output_v1_send_usable_area(resource, usable.x, usable.y, usable.width,
                                  usable.height);
}

void UkuiProtocolManager::Impl::DestroyOutputBinding(wl_resource* resource) {
  auto* binding =
      static_cast<OutputBinding*>(wl_resource_get_user_data(resource));
  if (binding == nullptr) {
    return;
  }
  if (binding->output != nullptr) {
    std::erase(binding->output->bindings, binding);
  }
  delete binding;
}

void UkuiProtocolManager::Impl::FinishOutput(Output* output) {
  for (OutputBinding* binding : output->bindings) {
    ukui_output_v1_send_finished(binding->resource);
    wl_resource_set_user_data(binding->resource, nullptr);
    binding->output = nullptr;
    delete binding;
  }
  output->bindings.clear();
}

void UkuiProtocolManager::Impl::OnOutputDestroy(wl_listener* listener, void*) {
  Output* output = wl_container_of(listener, output, destroy);
  output->manager->RemoveOutput(output);
}

void UkuiProtocolManager::Impl::RemoveOutput(Output* output) {
  if (output == nullptr) {
    return;
  }
  FinishOutput(output);
  if (output->handle != nullptr) {
    wl_list_remove(&output->destroy.link);
    output->handle = nullptr;
  }
  std::erase_if(outputs,
                [output](const auto& item) { return item.get() == output; });
}

UkuiProtocolManager::Impl::Output* UkuiProtocolManager::Impl::FindOutput(
    wlr_output* handle) const {
  auto it = std::find_if(
      outputs.begin(), outputs.end(),
      [handle](const auto& item) { return item->handle == handle; });
  return it == outputs.end() ? nullptr : it->get();
}

}  // namespace protocol
}  // namespace flakewm
