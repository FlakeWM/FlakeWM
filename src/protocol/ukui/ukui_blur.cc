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

#include "protocol/ukui-blur-v1-protocol.h"
#include "src/core/compositor_private/compositor_private.h"
#include "src/protocol/blur_level.h"
#include "src/protocol/ukui/ukui_protocol_manager_internal.h"
#include "src/render/backdrop_blur_renderer.h"

namespace flakewm {
namespace protocol {
namespace {
using ukui_internal::kBlurVersion;
}  // namespace

void UkuiProtocolManager::Impl::BindBlurManager(wl_client* client, void* data,
                                                uint32_t version, uint32_t id) {
  static const struct ukui_blur_manager_v1_interface implementation = {
      .destroy = DestroyResourceRequest,
      .get_blur = CreateBlur,
  };
  wl_resource* resource =
      wl_resource_create(client, &ukui_blur_manager_v1_interface,
                         static_cast<int>(std::min(version, kBlurVersion)), id);
  if (resource == nullptr) {
    wl_client_post_no_memory(client);
    return;
  }
  wl_resource_set_implementation(resource, &implementation, data, nullptr);
}

void UkuiProtocolManager::Impl::CreateBlur(wl_client* client,
                                           wl_resource* manager_resource,
                                           uint32_t id,
                                           wl_resource* surface_resource) {
  static const struct ukui_blur_surface_v1_interface implementation = {
      .destroy = DestroyResourceRequest,
      .set_region = SetBlurRegion,
      .set_level = SetBlurLevel,
  };
  auto* manager =
      static_cast<Impl*>(wl_resource_get_user_data(manager_resource));
  wlr_surface* surface = wlr_surface_from_resource(surface_resource);
  if (surface == nullptr) return;
  auto existing =
      std::find_if(manager->blurs.begin(), manager->blurs.end(),
                   [surface](Blur* blur) { return blur->surface == surface; });
  if (existing != manager->blurs.end()) {
    wl_resource_post_error(manager_resource,
                           UKUI_BLUR_MANAGER_V1_ERROR_BLUR_EXISTS,
                           "ukui blur surface already exists for this surface");
    return;
  }
  wl_resource* resource =
      wl_resource_create(client, &ukui_blur_surface_v1_interface,
                         wl_resource_get_version(manager_resource), id);
  if (resource == nullptr) {
    wl_client_post_no_memory(client);
    return;
  }
  auto* blur =
      new Blur{.manager = manager, .resource = resource, .surface = surface};
  pixman_region32_init(&blur->current_region);
  pixman_region32_init(&blur->pending_region);
  blur->commit.notify = OnBlurCommit;
  wl_signal_add(&surface->events.commit, &blur->commit);
  blur->destroy.notify = OnBlurSurfaceDestroy;
  wl_signal_add(&surface->events.destroy, &blur->destroy);
  manager->blurs.push_back(blur);
  wl_resource_set_implementation(resource, &implementation, blur, DestroyBlur);
}

// The callback parameter order is fixed by the generated Wayland ABI.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void UkuiProtocolManager::Impl::SetBlurRegion(wl_client*, wl_resource* resource,
                                              wl_resource* region_resource) {
  auto* blur = static_cast<Blur*>(wl_resource_get_user_data(resource));
  if (blur == nullptr) return;
  blur->pending_region_set = true;
  blur->pending_infinite = region_resource == nullptr;
  if (region_resource == nullptr) {
    pixman_region32_clear(&blur->pending_region);
  } else if (const pixman_region32_t* region =
                 wlr_region_from_resource(region_resource);
             region != nullptr) {
    pixman_region32_copy(&blur->pending_region, region);
  }
  blur->pending |= 1;
}

void UkuiProtocolManager::Impl::SetBlurLevel(wl_client*, wl_resource* resource,
                                             uint32_t level) {
  auto* blur = static_cast<Blur*>(wl_resource_get_user_data(resource));
  if (blur == nullptr || level < 1 || level > 15) return;
  blur->pending_level = level;
  blur->pending |= 2;
}

void UkuiProtocolManager::Impl::OnBlurCommit(wl_listener* listener, void*) {
  Blur* blur = wl_container_of(listener, blur, commit);
  if ((blur->pending & 1) != 0) {
    pixman_region32_copy(&blur->current_region, &blur->pending_region);
    blur->current_region_set = blur->pending_region_set;
    blur->current_infinite = blur->pending_infinite;
  }
  if ((blur->pending & 2) != 0) blur->current_level = blur->pending_level;
  if (blur->pending != 0) blur->manager->ApplyBlur(blur);
  blur->pending = 0;
}

void UkuiProtocolManager::Impl::ApplyBlur(Blur* blur) const {
  if (blur == nullptr || blur->surface == nullptr ||
      compositor->backdrop_blur_renderer_ == nullptr) {
    return;
  }
  if (!blur->current_region_set ||
      (!blur->current_infinite &&
       pixman_region32_empty(&blur->current_region))) {
    compositor->backdrop_blur_renderer_->ClearSurfaceBlur(blur->surface);
  } else {
    const BlurLevel& level =
        BlurLevelFor(static_cast<int>(blur->current_level));
    compositor->backdrop_blur_renderer_->SetSurfaceBlur(
        blur->surface, &blur->current_region, level.offset, level.iterations);
  }
  compositor->UpdateBackdropBlurState();
}

void UkuiProtocolManager::Impl::OnBlurSurfaceDestroy(wl_listener* listener,
                                                     void*) {
  Blur* blur = wl_container_of(listener, blur, destroy);
  if (blur->manager->compositor->backdrop_blur_renderer_ != nullptr) {
    blur->manager->compositor->backdrop_blur_renderer_->ClearSurfaceBlur(
        blur->surface);
    blur->manager->compositor->UpdateBackdropBlurState();
  }
  blur->surface = nullptr;
  wl_list_remove(&blur->commit.link);
  wl_list_remove(&blur->destroy.link);
}

void UkuiProtocolManager::Impl::DestroyBlur(wl_resource* resource) {
  auto* blur = static_cast<Blur*>(wl_resource_get_user_data(resource));
  if (blur == nullptr) return;
  if (blur->surface != nullptr) {
    wl_list_remove(&blur->commit.link);
    wl_list_remove(&blur->destroy.link);
    if (blur->manager->compositor->backdrop_blur_renderer_ != nullptr) {
      blur->manager->compositor->backdrop_blur_renderer_->ClearSurfaceBlur(
          blur->surface);
      blur->manager->compositor->UpdateBackdropBlurState();
    }
  }
  pixman_region32_fini(&blur->current_region);
  pixman_region32_fini(&blur->pending_region);
  std::erase(blur->manager->blurs, blur);
  delete blur;
}

}  // namespace protocol
}  // namespace flakewm
