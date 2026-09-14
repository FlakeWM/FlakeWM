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

#include "src/protocol/kde/kde_output_manager.h"

#include <absl/log/absl_log.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <vector>

#include "protocol/dpms-protocol.h"
#include "protocol/kde-output-device-v2-protocol.h"
#include "protocol/kde-output-management-v2-protocol.h"
#include "protocol/kde-primary-output-v1-protocol.h"
#include "src/core/compositor_private/compositor_private.h"

namespace flakewm {
namespace protocol {
namespace {

constexpr uint32_t kOutputDeviceVersion = 2;
constexpr uint32_t kOutputManagementVersion = 2;
constexpr uint32_t kPrimaryOutputVersion = 2;
constexpr uint32_t kDpmsVersion = 1;
constexpr int32_t kMaximumSceneCoordinate =
    std::numeric_limits<int32_t>::max() / 4;

const char* Safe(const char* value) { return value == nullptr ? "" : value; }

}  // namespace

struct KdeOutputManager::ModeBinding {
  DeviceBinding* device;
  wl_resource* resource;
  wlr_output_mode* mode;
};

struct KdeOutputManager::DeviceBinding {
  Output* output;
  wl_resource* resource;
  std::vector<ModeBinding*> modes;
};

struct KdeOutputManager::DpmsBinding {
  Output* output;
  wl_resource* resource;
};

struct KdeOutputManager::Output {
  KdeOutputManager* manager;
  wlr_output* handle;
  wl_global* global = nullptr;
  std::vector<DeviceBinding*> devices;
  std::vector<DpmsBinding*> dpms;
  wl_listener commit = {};
  wl_listener destroy = {};
};

struct KdeOutputManager::ConfigurationHead {
  Output* output;
  bool enabled;
  wlr_output_mode* mode;
  wl_output_transform transform;
  float scale;
  bool adaptive_sync;
  int x;
  int y;
};

struct KdeOutputManager::Configuration {
  KdeOutputManager* manager;
  wl_resource* resource;
  std::vector<ConfigurationHead> heads;
  Output* primary;
  bool applied = false;
};

KdeOutputManager::KdeOutputManager(core::CompositorPrivate* compositor,
                                   wl_display* display, wlr_backend* backend,
                                   wlr_output_layout* output_layout)
    : compositor_(compositor),
      display_(display),
      backend_(backend),
      output_layout_(output_layout) {
  management_global_ =
      wl_global_create(display_, &kde_output_management_v2_interface,
                       kOutputManagementVersion, this, BindManagement);
  primary_global_ = wl_global_create(display_, &kde_primary_output_v1_interface,
                                     kPrimaryOutputVersion, this, BindPrimary);
  dpms_global_ =
      wl_global_create(display_, &org_kde_kwin_dpms_manager_interface,
                       kDpmsVersion, this, BindDpmsManager);
}

KdeOutputManager::~KdeOutputManager() {
  if (management_global_ != nullptr) {
    wl_global_destroy(management_global_);
  }
  if (primary_global_ != nullptr) {
    wl_global_destroy(primary_global_);
  }
  if (dpms_global_ != nullptr) {
    wl_global_destroy(dpms_global_);
  }
  while (!outputs_.empty()) {
    RemoveOutput(outputs_.back());
  }
}

bool KdeOutputManager::IsValid() const {
  return management_global_ != nullptr && primary_global_ != nullptr &&
         dpms_global_ != nullptr;
}

void KdeOutputManager::AddOutput(wlr_output* handle) {
  if (handle == nullptr || FindOutput(handle) != nullptr) {
    return;
  }
  auto* output = new Output{
      .manager = this,
      .handle = handle,
  };
  output->global = wl_global_create(display_, &kde_output_device_v2_interface,
                                    kOutputDeviceVersion, output, BindOutput);
  if (output->global == nullptr) {
    delete output;
    return;
  }
  output->commit.notify = OnOutputCommit;
  wl_signal_add(&handle->events.commit, &output->commit);
  output->destroy.notify = OnOutputDestroy;
  wl_signal_add(&handle->events.destroy, &output->destroy);
  outputs_.push_back(output);
  if (primary_output_ == nullptr && handle->enabled) {
    SetPrimary(output);
  }
}

void KdeOutputManager::UpdateOutputs() {
  for (Output* output : outputs_) {
    for (DeviceBinding* binding : output->devices) {
      SendOutput(binding, false);
    }
    SendDpms(output);
  }
  if (primary_output_ == nullptr || primary_output_->handle == nullptr ||
      !primary_output_->handle->enabled) {
    auto it = std::find_if(outputs_.begin(), outputs_.end(), [](Output* item) {
      return item->handle != nullptr && item->handle->enabled;
    });
    SetPrimary(it == outputs_.end() ? nullptr : *it);
  }
}

void KdeOutputManager::BindManagement(wl_client* client, void* data,
                                      uint32_t version, uint32_t id) {
  static const struct kde_output_management_v2_interface implementation = {
      .create_configuration = CreateConfiguration,
  };
  wl_resource* resource =
      wl_resource_create(client, &kde_output_management_v2_interface,
                         std::min(version, kOutputManagementVersion), id);
  if (resource == nullptr) {
    wl_client_post_no_memory(client);
    return;
  }
  wl_resource_set_implementation(resource, &implementation, data, nullptr);
}

void KdeOutputManager::CreateConfiguration(wl_client* client,
                                           wl_resource* resource, uint32_t id) {
  static const struct kde_output_configuration_v2_interface implementation = {
      .enable = ConfigureEnable,
      .mode = ConfigureMode,
      .transform = ConfigureTransform,
      .position = ConfigurePosition,
      .scale = ConfigureScale,
      .apply = ConfigureApply,
      .destroy = ConfigureDestroy,
      .overscan = ConfigureOverscan,
      .set_vrr_policy = ConfigureVrr,
      .set_rgb_range = ConfigureRgbRange,
      .set_primary_output = ConfigurePrimary,
  };
  auto* manager =
      static_cast<KdeOutputManager*>(wl_resource_get_user_data(resource));
  wl_resource* configuration_resource =
      wl_resource_create(client, &kde_output_configuration_v2_interface,
                         wl_resource_get_version(resource), id);
  if (configuration_resource == nullptr) {
    wl_client_post_no_memory(client);
    return;
  }
  auto* configuration = new Configuration{
      .manager = manager,
      .resource = configuration_resource,
      .primary = manager->primary_output_,
  };
  manager->configurations_.push_back(configuration);
  wl_resource_set_implementation(configuration_resource, &implementation,
                                 configuration, DestroyConfiguration);
}

void KdeOutputManager::DestroyConfiguration(wl_resource* resource) {
  auto* configuration =
      static_cast<Configuration*>(wl_resource_get_user_data(resource));
  if (configuration == nullptr) {
    return;
  }
  std::erase(configuration->manager->configurations_, configuration);
  delete configuration;
}

void KdeOutputManager::ConfigureEnable(wl_client*, wl_resource* resource,
                                       wl_resource* output, int32_t enabled) {
  auto* configuration =
      static_cast<Configuration*>(wl_resource_get_user_data(resource));
  if (ConfigurationHead* head =
          configuration->manager->GetHead(configuration, output)) {
    head->enabled = enabled != 0;
  }
}

void KdeOutputManager::ConfigureMode(wl_client*, wl_resource* resource,
                                     wl_resource* output,
                                     wl_resource* mode_resource) {
  auto* configuration =
      static_cast<Configuration*>(wl_resource_get_user_data(resource));
  ConfigurationHead* head =
      configuration->manager->GetHead(configuration, output);
  auto* mode =
      static_cast<ModeBinding*>(wl_resource_get_user_data(mode_resource));
  if (head != nullptr && mode != nullptr && mode->device != nullptr &&
      mode->device->output == head->output) {
    head->mode = mode->mode;
  }
}

void KdeOutputManager::ConfigureTransform(wl_client*, wl_resource* resource,
                                          wl_resource* output,
                                          int32_t transform) {
  auto* configuration =
      static_cast<Configuration*>(wl_resource_get_user_data(resource));
  if (ConfigurationHead* head =
          configuration->manager->GetHead(configuration, output);
      head != nullptr && transform >= WL_OUTPUT_TRANSFORM_NORMAL &&
      transform <= WL_OUTPUT_TRANSFORM_FLIPPED_270) {
    head->transform = static_cast<wl_output_transform>(transform);
  }
}

void KdeOutputManager::ConfigurePosition(wl_client*, wl_resource* resource,
                                         wl_resource* output, int32_t x,
                                         int32_t y) {
  auto* configuration =
      static_cast<Configuration*>(wl_resource_get_user_data(resource));
  if (ConfigurationHead* head =
          configuration->manager->GetHead(configuration, output)) {
    head->x = std::clamp(x, -kMaximumSceneCoordinate, kMaximumSceneCoordinate);
    head->y = std::clamp(y, -kMaximumSceneCoordinate, kMaximumSceneCoordinate);
  }
}

void KdeOutputManager::ConfigureScale(wl_client*, wl_resource* resource,
                                      wl_resource* output, wl_fixed_t scale) {
  auto* configuration =
      static_cast<Configuration*>(wl_resource_get_user_data(resource));
  const double value = wl_fixed_to_double(scale);
  if (ConfigurationHead* head =
          configuration->manager->GetHead(configuration, output);
      head != nullptr && std::isfinite(value) && value > 0.0) {
    head->scale = static_cast<float>(value);
  }
}

void KdeOutputManager::ConfigureApply(wl_client*, wl_resource* resource) {
  auto* configuration =
      static_cast<Configuration*>(wl_resource_get_user_data(resource));
  if (configuration->applied) {
    wl_resource_post_error(resource,
                           KDE_OUTPUT_CONFIGURATION_V2_ERROR_ALREADY_APPLIED,
                           "an output configuration can only be applied once");
    return;
  }
  configuration->applied = true;
  configuration->manager->Apply(configuration);
}

void KdeOutputManager::ConfigureDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void KdeOutputManager::ConfigureOverscan(wl_client*, wl_resource*, wl_resource*,
                                         uint32_t) {
  // wlroots has no backend-neutral overscan property.
}

void KdeOutputManager::ConfigureVrr(wl_client*, wl_resource* resource,
                                    wl_resource* output, uint32_t policy) {
  auto* configuration =
      static_cast<Configuration*>(wl_resource_get_user_data(resource));
  if (ConfigurationHead* head =
          configuration->manager->GetHead(configuration, output)) {
    head->adaptive_sync =
        policy != KDE_OUTPUT_CONFIGURATION_V2_VRR_POLICY_NEVER;
  }
}

void KdeOutputManager::ConfigureRgbRange(wl_client*, wl_resource*, wl_resource*,
                                         uint32_t) {
  // wlroots has no backend-neutral connector RGB-range state.
}

void KdeOutputManager::ConfigurePrimary(wl_client*, wl_resource* resource,
                                        wl_resource* output_resource) {
  auto* configuration =
      static_cast<Configuration*>(wl_resource_get_user_data(resource));
  auto* binding =
      static_cast<DeviceBinding*>(wl_resource_get_user_data(output_resource));
  if (binding != nullptr && binding->output != nullptr) {
    configuration->primary = binding->output;
  }
}

void KdeOutputManager::BindPrimary(wl_client* client, void* data,
                                   uint32_t version, uint32_t id) {
  static const struct kde_primary_output_v1_interface implementation = {
      .destroy = DestroyPrimary,
  };
  auto* manager = static_cast<KdeOutputManager*>(data);
  wl_resource* resource =
      wl_resource_create(client, &kde_primary_output_v1_interface,
                         std::min(version, kPrimaryOutputVersion), id);
  if (resource == nullptr) {
    wl_client_post_no_memory(client);
    return;
  }
  manager->primary_resources_.push_back(resource);
  wl_resource_set_implementation(resource, &implementation, manager,
                                 RemovePrimaryResource);
  if (manager->primary_output_ != nullptr &&
      manager->primary_output_->handle != nullptr) {
    kde_primary_output_v1_send_primary_output(
        resource, Safe(manager->primary_output_->handle->name));
  }
}

void KdeOutputManager::DestroyPrimary(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void KdeOutputManager::RemovePrimaryResource(wl_resource* resource) {
  auto* manager =
      static_cast<KdeOutputManager*>(wl_resource_get_user_data(resource));
  if (manager != nullptr) {
    std::erase(manager->primary_resources_, resource);
  }
}

void KdeOutputManager::BindDpmsManager(wl_client* client, void* data,
                                       uint32_t version, uint32_t id) {
  static const struct org_kde_kwin_dpms_manager_interface implementation = {
      .get = GetDpms,
  };
  wl_resource* resource =
      wl_resource_create(client, &org_kde_kwin_dpms_manager_interface,
                         std::min(version, kDpmsVersion), id);
  if (resource == nullptr) {
    wl_client_post_no_memory(client);
    return;
  }
  wl_resource_set_implementation(resource, &implementation, data, nullptr);
}

void KdeOutputManager::GetDpms(wl_client* client, wl_resource* resource,
                               uint32_t id, wl_resource* output_resource) {
  static const struct org_kde_kwin_dpms_interface implementation = {
      .set = SetDpms,
      .release = ReleaseDpms,
  };
  auto* manager =
      static_cast<KdeOutputManager*>(wl_resource_get_user_data(resource));
  wlr_output* handle = wlr_output_from_resource(output_resource);
  Output* output = manager->FindOutput(handle);
  wl_resource* dpms_resource =
      wl_resource_create(client, &org_kde_kwin_dpms_interface,
                         wl_resource_get_version(resource), id);
  if (dpms_resource == nullptr) {
    wl_client_post_no_memory(client);
    return;
  }
  if (output == nullptr) {
    wl_resource_set_implementation(dpms_resource, &implementation, nullptr,
                                   nullptr);
    return;
  }
  auto* binding = new DpmsBinding{
      .output = output,
      .resource = dpms_resource,
  };
  output->dpms.push_back(binding);
  wl_resource_set_implementation(dpms_resource, &implementation, binding,
                                 DestroyDpms);
  org_kde_kwin_dpms_send_supported(dpms_resource, 1);
  org_kde_kwin_dpms_send_mode(dpms_resource, output->handle->enabled
                                                 ? ORG_KDE_KWIN_DPMS_MODE_ON
                                                 : ORG_KDE_KWIN_DPMS_MODE_OFF);
  org_kde_kwin_dpms_send_done(dpms_resource);
}

void KdeOutputManager::SetDpms(wl_client*, wl_resource* resource,
                               uint32_t mode) {
  auto* binding =
      static_cast<DpmsBinding*>(wl_resource_get_user_data(resource));
  if (binding == nullptr || binding->output == nullptr ||
      binding->output->handle == nullptr) {
    return;
  }
  wlr_output_state state = {};
  wlr_output_state_init(&state);
  wlr_output_state_set_enabled(&state, mode == ORG_KDE_KWIN_DPMS_MODE_ON);
  if (!wlr_output_commit_state(binding->output->handle, &state)) {
    ABSL_LOG(WARNING) << "KDE DPMS request was rejected for "
                      << Safe(binding->output->handle->name);
  }
  wlr_output_state_finish(&state);
  binding->output->manager->UpdateOutputs();
}

void KdeOutputManager::ReleaseDpms(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void KdeOutputManager::DestroyDpms(wl_resource* resource) {
  auto* binding =
      static_cast<DpmsBinding*>(wl_resource_get_user_data(resource));
  if (binding == nullptr) {
    return;
  }
  if (binding->output != nullptr) {
    std::erase(binding->output->dpms, binding);
  }
  delete binding;
}

void KdeOutputManager::BindOutput(wl_client* client, void* data,
                                  uint32_t version, uint32_t id) {
  auto* output = static_cast<Output*>(data);
  wl_resource* resource =
      wl_resource_create(client, &kde_output_device_v2_interface,
                         std::min(version, kOutputDeviceVersion), id);
  if (resource == nullptr) {
    wl_client_post_no_memory(client);
    return;
  }
  auto* binding = new DeviceBinding{
      .output = output,
      .resource = resource,
  };
  output->devices.push_back(binding);
  wl_resource_set_implementation(resource, nullptr, binding,
                                 DestroyDeviceBinding);
  output->manager->SendOutput(binding, true);
}

void KdeOutputManager::DestroyDeviceBinding(wl_resource* resource) {
  auto* binding =
      static_cast<DeviceBinding*>(wl_resource_get_user_data(resource));
  if (binding == nullptr) {
    return;
  }
  while (!binding->modes.empty()) {
    wl_resource_destroy(binding->modes.back()->resource);
  }
  if (binding->output != nullptr) {
    std::erase(binding->output->devices, binding);
  }
  delete binding;
}

void KdeOutputManager::DestroyModeBinding(wl_resource* resource) {
  auto* binding =
      static_cast<ModeBinding*>(wl_resource_get_user_data(resource));
  if (binding == nullptr) {
    return;
  }
  if (binding->device != nullptr) {
    std::erase(binding->device->modes, binding);
  }
  delete binding;
}

void KdeOutputManager::OnOutputCommit(wl_listener* listener, void*) {
  Output* output = wl_container_of(listener, output, commit);
  for (DeviceBinding* binding : output->devices) {
    output->manager->SendOutput(binding, false);
  }
  output->manager->SendDpms(output);
}

void KdeOutputManager::OnOutputDestroy(wl_listener* listener, void*) {
  Output* output = wl_container_of(listener, output, destroy);
  output->manager->RemoveOutput(output);
}

KdeOutputManager::ConfigurationHead* KdeOutputManager::GetHead(
    Configuration* configuration, wl_resource* output_resource) {
  auto* binding =
      static_cast<DeviceBinding*>(wl_resource_get_user_data(output_resource));
  if (binding == nullptr || binding->output == nullptr ||
      binding->output->handle == nullptr) {
    return nullptr;
  }
  const auto existing =
      std::find_if(configuration->heads.begin(), configuration->heads.end(),
                   [binding](const ConfigurationHead& head) {
                     return head.output == binding->output;
                   });
  if (existing != configuration->heads.end()) {
    return &*existing;
  }
  wlr_box box = {};
  wlr_output_layout_get_box(output_layout_, binding->output->handle, &box);
  configuration->heads.push_back(ConfigurationHead{
      .output = binding->output,
      .enabled = binding->output->handle->enabled,
      .mode = binding->output->handle->current_mode,
      .transform = binding->output->handle->transform,
      .scale = binding->output->handle->scale,
      .adaptive_sync = binding->output->handle->adaptive_sync_status ==
                       WLR_OUTPUT_ADAPTIVE_SYNC_ENABLED,
      .x = box.x,
      .y = box.y,
  });
  return &configuration->heads.back();
}

KdeOutputManager::Output* KdeOutputManager::FindOutput(
    wlr_output* handle) const {
  auto it = std::find_if(
      outputs_.begin(), outputs_.end(),
      [handle](Output* output) { return output->handle == handle; });
  return it == outputs_.end() ? nullptr : *it;
}

void KdeOutputManager::SendOutput(DeviceBinding* binding, bool include_modes) {
  if (binding == nullptr || binding->output == nullptr ||
      binding->output->handle == nullptr) {
    return;
  }
  wlr_output* output = binding->output->handle;
  wlr_box box = {};
  wlr_output_layout_get_box(output_layout_, output, &box);
  kde_output_device_v2_send_enabled(binding->resource, output->enabled);
  kde_output_device_v2_send_geometry(
      binding->resource, box.x, box.y, output->phys_width, output->phys_height,
      static_cast<int32_t>(output->subpixel), Safe(output->make),
      Safe(output->model), static_cast<int32_t>(output->transform));

  if (include_modes) {
    auto add_mode = [binding](wlr_output_mode* mode) {
      wl_resource* resource =
          wl_resource_create(wl_resource_get_client(binding->resource),
                             &kde_output_device_mode_v2_interface, 1, 0);
      if (resource == nullptr) {
        return;
      }
      auto* mode_binding = new ModeBinding{
          .device = binding,
          .resource = resource,
          .mode = mode,
      };
      binding->modes.push_back(mode_binding);
      wl_resource_set_implementation(resource, nullptr, mode_binding,
                                     DestroyModeBinding);
      kde_output_device_v2_send_mode(binding->resource, resource);
      kde_output_device_mode_v2_send_size(resource, mode->width, mode->height);
      kde_output_device_mode_v2_send_refresh(resource, mode->refresh);
      if (mode->preferred) {
        kde_output_device_mode_v2_send_preferred(resource);
      }
    };
    if (!wl_list_empty(&output->modes)) {
      wlr_output_mode* mode = nullptr;
      wl_list_for_each(mode, &output->modes, link) { add_mode(mode); }
    }
    if (binding->modes.empty()) {
      // Headless and fbdev outputs can expose a custom mode with no mode list.
      // Store no selectable mode object, but still publish all other state.
      ABSL_LOG(INFO) << "KDE output " << Safe(output->name)
                     << " has no fixed mode list";
    }
  }

  if (output->enabled && output->current_mode != nullptr) {
    auto it = std::find_if(binding->modes.begin(), binding->modes.end(),
                           [output](ModeBinding* mode) {
                             return mode->mode == output->current_mode;
                           });
    if (it != binding->modes.end()) {
      kde_output_device_v2_send_current_mode(binding->resource,
                                             (*it)->resource);
    }
  }
  kde_output_device_v2_send_scale(binding->resource,
                                  wl_fixed_from_double(output->scale));
  kde_output_device_v2_send_edid(binding->resource, "");
  kde_output_device_v2_send_uuid(binding->resource, Safe(output->name));
  kde_output_device_v2_send_serial_number(binding->resource,
                                          Safe(output->serial));
  uint32_t capabilities =
      output->adaptive_sync_supported ? KDE_OUTPUT_DEVICE_V2_CAPABILITY_VRR : 0;
  kde_output_device_v2_send_capabilities(binding->resource, capabilities);
  if (output->adaptive_sync_supported) {
    kde_output_device_v2_send_vrr_policy(
        binding->resource,
        output->adaptive_sync_status == WLR_OUTPUT_ADAPTIVE_SYNC_ENABLED
            ? KDE_OUTPUT_DEVICE_V2_VRR_POLICY_ALWAYS
            : KDE_OUTPUT_DEVICE_V2_VRR_POLICY_NEVER);
  }
  if (wl_resource_get_version(binding->resource) >=
      KDE_OUTPUT_DEVICE_V2_NAME_SINCE_VERSION) {
    kde_output_device_v2_send_name(binding->resource, Safe(output->name));
  }
  kde_output_device_v2_send_done(binding->resource);
}

void KdeOutputManager::SendDpms(Output* output) {
  if (output == nullptr || output->handle == nullptr) {
    return;
  }
  for (DpmsBinding* binding : output->dpms) {
    org_kde_kwin_dpms_send_mode(binding->resource,
                                output->handle->enabled
                                    ? ORG_KDE_KWIN_DPMS_MODE_ON
                                    : ORG_KDE_KWIN_DPMS_MODE_OFF);
    org_kde_kwin_dpms_send_done(binding->resource);
  }
}

void KdeOutputManager::Apply(Configuration* configuration) {
  std::vector<wlr_backend_output_state> states(configuration->heads.size());
  for (size_t index = 0; index < configuration->heads.size(); ++index) {
    const ConfigurationHead& head = configuration->heads[index];
    states[index].output = head.output->handle;
    wlr_output_state_init(&states[index].base);
    wlr_output_state_set_enabled(&states[index].base, head.enabled);
    if (head.mode != nullptr) {
      wlr_output_state_set_mode(&states[index].base, head.mode);
    }
    wlr_output_state_set_transform(&states[index].base, head.transform);
    wlr_output_state_set_scale(&states[index].base, head.scale);
    if (head.output->handle->adaptive_sync_supported) {
      wlr_output_state_set_adaptive_sync_enabled(&states[index].base,
                                                 head.adaptive_sync);
    }
  }
  const bool accepted =
      states.empty() ||
      (wlr_backend_test(backend_, states.data(), states.size()) &&
       wlr_backend_commit(backend_, states.data(), states.size()));
  for (wlr_backend_output_state& state : states) {
    wlr_output_state_finish(&state.base);
  }
  if (!accepted) {
    kde_output_configuration_v2_send_failed(configuration->resource);
    return;
  }
  for (const ConfigurationHead& head : configuration->heads) {
    if (head.output->handle != nullptr) {
      wlr_output_layout_add(output_layout_, head.output->handle, head.x,
                            head.y);
    }
  }
  SetPrimary(configuration->primary);
  for (const std::unique_ptr<core::CompositorPrivate::Output>& output :
       compositor_->outputs_) {
    compositor_->ArrangeLayers(output.get());
  }
  UpdateOutputs();
  if (compositor_->protocol_manager_ != nullptr) {
    compositor_->protocol_manager_->UpdateOutputs();
  }
  kde_output_configuration_v2_send_applied(configuration->resource);
}

void KdeOutputManager::RemoveOutput(Output* output) {
  if (output == nullptr) {
    return;
  }
  if (output->global != nullptr) {
    wl_global_destroy(output->global);
    output->global = nullptr;
  }
  output->commit.notify = nullptr;
  output->destroy.notify = nullptr;
  wl_list_remove(&output->commit.link);
  wl_list_remove(&output->destroy.link);
  while (!output->devices.empty()) {
    wl_resource_destroy(output->devices.back()->resource);
  }
  while (!output->dpms.empty()) {
    wl_resource_destroy(output->dpms.back()->resource);
  }
  for (Configuration* configuration : configurations_) {
    std::erase_if(configuration->heads,
                  [output](const ConfigurationHead& head) {
                    return head.output == output;
                  });
    if (configuration->primary == output) {
      configuration->primary = nullptr;
    }
  }
  const bool was_primary = primary_output_ == output;
  std::erase(outputs_, output);
  delete output;
  if (was_primary) {
    primary_output_ = nullptr;
    auto it = std::find_if(outputs_.begin(), outputs_.end(), [](Output* item) {
      return item->handle != nullptr && item->handle->enabled;
    });
    SetPrimary(it == outputs_.end() ? nullptr : *it);
  }
}

void KdeOutputManager::SetPrimary(Output* output) {
  if (primary_output_ == output) {
    return;
  }
  primary_output_ = output;
  if (output == nullptr || output->handle == nullptr) {
    return;
  }
  for (wl_resource* resource : primary_resources_) {
    kde_primary_output_v1_send_primary_output(resource,
                                              Safe(output->handle->name));
  }
}

}  // namespace protocol
}  // namespace flakewm
