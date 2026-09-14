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

#ifndef SRC_PROTOCOL_KDE_KDE_OUTPUT_MANAGER_H_
#define SRC_PROTOCOL_KDE_KDE_OUTPUT_MANAGER_H_

#include <vector>

#include "src/wlr_wrapper/wlroots.h"

namespace flakewm {
namespace core {
class CompositorPrivate;
}  // namespace core

namespace protocol {

class KdeOutputManager final {
 public:
  KdeOutputManager(core::CompositorPrivate* compositor, wl_display* display,
                   wlr_backend* backend, wlr_output_layout* output_layout);
  ~KdeOutputManager();

  KdeOutputManager(const KdeOutputManager&) = delete;
  KdeOutputManager& operator=(const KdeOutputManager&) = delete;

  bool IsValid() const;
  void AddOutput(wlr_output* output);
  void UpdateOutputs();

 private:
  struct Output;
  struct DeviceBinding;
  struct ModeBinding;
  struct DpmsBinding;
  struct Configuration;
  struct ConfigurationHead;

  static void BindManagement(wl_client* client, void* data, uint32_t version,
                             uint32_t id);
  static void CreateConfiguration(wl_client* client, wl_resource* resource,
                                  uint32_t id);
  static void DestroyConfiguration(wl_resource* resource);
  static void ConfigureEnable(wl_client* client, wl_resource* resource,
                              wl_resource* output, int32_t enabled);
  static void ConfigureMode(wl_client* client, wl_resource* resource,
                            wl_resource* output, wl_resource* mode);
  static void ConfigureTransform(wl_client* client, wl_resource* resource,
                                 wl_resource* output, int32_t transform);
  static void ConfigurePosition(wl_client* client, wl_resource* resource,
                                wl_resource* output, int32_t x, int32_t y);
  static void ConfigureScale(wl_client* client, wl_resource* resource,
                             wl_resource* output, wl_fixed_t scale);
  static void ConfigureApply(wl_client* client, wl_resource* resource);
  static void ConfigureDestroy(wl_client* client, wl_resource* resource);
  static void ConfigureOverscan(wl_client* client, wl_resource* resource,
                                wl_resource* output, uint32_t overscan);
  static void ConfigureVrr(wl_client* client, wl_resource* resource,
                           wl_resource* output, uint32_t policy);
  static void ConfigureRgbRange(wl_client* client, wl_resource* resource,
                                wl_resource* output, uint32_t range);
  static void ConfigurePrimary(wl_client* client, wl_resource* resource,
                               wl_resource* output);

  static void BindPrimary(wl_client* client, void* data, uint32_t version,
                          uint32_t id);
  static void DestroyPrimary(wl_client* client, wl_resource* resource);
  static void RemovePrimaryResource(wl_resource* resource);

  static void BindDpmsManager(wl_client* client, void* data, uint32_t version,
                              uint32_t id);
  static void GetDpms(wl_client* client, wl_resource* resource, uint32_t id,
                      wl_resource* output);
  static void SetDpms(wl_client* client, wl_resource* resource, uint32_t mode);
  static void ReleaseDpms(wl_client* client, wl_resource* resource);
  static void DestroyDpms(wl_resource* resource);

  static void BindOutput(wl_client* client, void* data, uint32_t version,
                         uint32_t id);
  static void DestroyDeviceBinding(wl_resource* resource);
  static void DestroyModeBinding(wl_resource* resource);
  static void OnOutputCommit(wl_listener* listener, void* data);
  static void OnOutputDestroy(wl_listener* listener, void* data);

  ConfigurationHead* GetHead(Configuration* configuration,
                             wl_resource* output_resource);
  Output* FindOutput(wlr_output* output) const;
  void SendOutput(DeviceBinding* binding, bool include_modes);
  void SendDpms(Output* output);
  void Apply(Configuration* configuration);
  void RemoveOutput(Output* output);
  void SetPrimary(Output* output);

  core::CompositorPrivate* compositor_;
  wl_display* display_;
  wlr_backend* backend_;
  wlr_output_layout* output_layout_;
  wl_global* management_global_ = nullptr;
  wl_global* primary_global_ = nullptr;
  wl_global* dpms_global_ = nullptr;
  Output* primary_output_ = nullptr;
  std::vector<Output*> outputs_;
  std::vector<Configuration*> configurations_;
  std::vector<wl_resource*> primary_resources_;
};

}  // namespace protocol
}  // namespace flakewm

#endif  // SRC_PROTOCOL_KDE_KDE_OUTPUT_MANAGER_H_
