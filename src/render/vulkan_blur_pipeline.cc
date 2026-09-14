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
 * Simple workaround so that UKUI blur could be also available when using
 * vulkan renders.
 * This file is entirely written by Codex.
 */

#include <drm_fourcc.h>

extern "C" {
#include <wlr/render/allocator.h>
#include <wlr/render/drm_format_set.h>
#include <wlr/render/interface.h>
#include <wlr/render/pass.h>
#include <wlr/render/vulkan.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_buffer.h>
}

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>

#include "absl/log/absl_log.h"
#include "src/render/vulkan_blur_pipeline.h"

namespace flakewm {
namespace render {
namespace {

struct BlurTap {
  float displacement;
  float weight;
};

// Gaussian kernel from GLES2 shader
constexpr std::array<BlurTap, 5> kBlurTaps = {{
    {0.0F, 0.2270270270F},
    {1.3846153846F, 0.3162162162F},
    {-1.3846153846F, 0.3162162162F},
    {3.2307692308F, 0.0702702703F},
    {-3.2307692308F, 0.0702702703F},
}};

constexpr std::array<uint32_t, 4> kBufferFormats = {
    DRM_FORMAT_ARGB8888,
    DRM_FORMAT_ABGR8888,
    DRM_FORMAT_XRGB8888,
    DRM_FORMAT_XBGR8888,
};

}  // namespace

class VulkanBlurPipeline::Impl {
 public:
  struct Target {
    wlr_buffer* buffer = nullptr;
    wlr_texture* texture = nullptr;
  };

  explicit Impl(wlr_renderer* new_renderer) : renderer(new_renderer) {}

  ~Impl() { ClearTargets(); }

  void SetAllocator(wlr_allocator* new_allocator) {
    if (allocator == new_allocator) {
      return;
    }
    ClearTargets();
    allocator = new_allocator;
  }

  bool IsSupported() const {
    if (renderer == nullptr || allocator == nullptr ||
        !wlr_renderer_is_vk(renderer)) {
      return false;
    }
    const wlr_drm_format_set* formats = RenderFormats();
    if (formats == nullptr) {
      return false;
    }
    return std::any_of(kBufferFormats.begin(), kBufferFormats.end(),
                       [formats](uint32_t format) {
                         return wlr_drm_format_set_get(formats, format) !=
                                nullptr;
                       });
  }

  wlr_texture* Render(wlr_buffer* source, float offset) {
    if (source == nullptr || source->width <= 0 || source->height <= 0 ||
        !EnsureTargets(source->width, source->height)) {
      return nullptr;
    }

    wlr_texture* source_texture = wlr_texture_from_buffer(renderer, source);
    if (source_texture == nullptr) {
      ABSL_LOG(ERROR) << "Failed to import Vulkan blur source buffer";
      return nullptr;
    }

    const float safe_offset = std::clamp(offset, 0.001F, 64.0F);
    bool success =
        DrawConvolution(source_texture, targets[0].buffer, safe_offset, 0.0F);
    wlr_texture_destroy(source_texture);
    if (!success) {
      return nullptr;
    }
    success = DrawConvolution(targets[0].texture, targets[1].buffer, 0.0F,
                              safe_offset);
    constexpr uint32_t kIterations = 3;
    for (uint32_t iteration = 1; success && iteration < kIterations;
         ++iteration) {
      success = DrawConvolution(targets[1].texture, targets[0].buffer,
                                safe_offset, 0.0F) &&
                DrawConvolution(targets[0].texture, targets[1].buffer, 0.0F,
                                safe_offset);
    }
    return success ? targets[1].texture : nullptr;
  }

 private:
  bool EnsureTargets(int width, int height) {
    if (targets[0].buffer != nullptr && target_width == width &&
        target_height == height) {
      return true;
    }
    ClearTargets();
    if (!IsSupported()) {
      return false;
    }

    const wlr_drm_format_set* formats = RenderFormats();
    for (uint32_t format_code : kBufferFormats) {
      const wlr_drm_format* format =
          wlr_drm_format_set_get(formats, format_code);
      if (format == nullptr) {
        continue;
      }
      targets[0].buffer =
          wlr_allocator_create_buffer(allocator, width, height, format);
      targets[1].buffer =
          wlr_allocator_create_buffer(allocator, width, height, format);
      if (targets[0].buffer == nullptr || targets[1].buffer == nullptr) {
        ClearTargets();
        continue;
      }
      targets[0].texture = wlr_texture_from_buffer(renderer, targets[0].buffer);
      targets[1].texture = wlr_texture_from_buffer(renderer, targets[1].buffer);
      if (targets[0].texture == nullptr || targets[1].texture == nullptr) {
        ClearTargets();
        continue;
      }
      target_width = width;
      target_height = height;
      return true;
    }
    ABSL_LOG(ERROR) << "Failed to allocate Vulkan backdrop blur buffers";
    return false;
  }

  const wlr_drm_format_set* RenderFormats() const {
    const wlr_renderer_impl* implementation = renderer->WLR_PRIVATE.impl;
    if (implementation == nullptr ||
        implementation->get_render_formats == nullptr) {
      return nullptr;
    }
    return implementation->get_render_formats(renderer);
  }

  bool DrawConvolution(wlr_texture* source, wlr_buffer* destination,
                       float horizontal, float vertical) const {
    wlr_render_pass* pass =
        wlr_renderer_begin_buffer_pass(renderer, destination, nullptr);
    if (pass == nullptr) {
      ABSL_LOG(ERROR) << "Failed to begin Vulkan backdrop blur pass";
      return false;
    }

    const wlr_render_rect_options clear = {
        .box =
            {
                .x = 0,
                .y = 0,
                .width = target_width,
                .height = target_height,
            },
        .color = {},
        .blend_mode = WLR_RENDER_BLEND_MODE_NONE,
    };
    wlr_render_pass_add_rect(pass, &clear);

    float accumulated_weight = 0.0F;
    for (const BlurTap& tap : kBlurTaps) {
      accumulated_weight += tap.weight;
      const float alpha = tap.weight / accumulated_weight;
      const int shift_x =
          static_cast<int>(std::lround(tap.displacement * horizontal));
      const int shift_y =
          static_cast<int>(std::lround(tap.displacement * vertical));
      DrawShiftedSample(pass, source, shift_x, shift_y, alpha);
    }
    if (!wlr_render_pass_submit(pass)) {
      ABSL_LOG(ERROR) << "Failed to submit Vulkan backdrop blur pass";
      return false;
    }
    return true;
  }

  void DrawShiftedSample(wlr_render_pass* pass, wlr_texture* source,
                         int shift_x, int shift_y, float alpha) const {
    const int dimension = shift_x == 0 ? target_height : target_width;
    const int shift = shift_x == 0 ? shift_y : shift_x;
    const int amount = std::min(std::abs(shift), dimension);

    auto add_sample = [pass, source, &alpha](const wlr_fbox& source_box,
                                             const wlr_box& destination_box) {
      if (destination_box.width <= 0 || destination_box.height <= 0) {
        return;
      }
      const wlr_render_texture_options options = {
          .texture = source,
          .src_box = source_box,
          .dst_box = destination_box,
          .alpha = &alpha,
          .filter_mode = WLR_SCALE_FILTER_BILINEAR,
          .blend_mode = WLR_RENDER_BLEND_MODE_PREMULTIPLIED,
          .transfer_function = WLR_COLOR_TRANSFER_FUNCTION_GAMMA22,
      };
      wlr_render_pass_add_texture(pass, &options);
    };

    if (shift_x == 0 && shift_y == 0) {
      add_sample(
          {.x = 0,
           .y = 0,
           .width = static_cast<double>(target_width),
           .height = static_cast<double>(target_height)},
          {.x = 0, .y = 0, .width = target_width, .height = target_height});
      return;
    }

    if (shift_x != 0) {
      const int content_width = target_width - amount;
      if (shift_x > 0) {
        add_sample(
            {.x = static_cast<double>(amount),
             .y = 0,
             .width = static_cast<double>(content_width),
             .height = static_cast<double>(target_height)},
            {.x = 0, .y = 0, .width = content_width, .height = target_height});
        add_sample({.x = static_cast<double>(target_width - 1),
                    .y = 0,
                    .width = 1,
                    .height = static_cast<double>(target_height)},
                   {.x = content_width,
                    .y = 0,
                    .width = amount,
                    .height = target_height});
      } else {
        add_sample({.x = 0,
                    .y = 0,
                    .width = static_cast<double>(content_width),
                    .height = static_cast<double>(target_height)},
                   {.x = amount,
                    .y = 0,
                    .width = content_width,
                    .height = target_height});
        add_sample({.x = 0,
                    .y = 0,
                    .width = 1,
                    .height = static_cast<double>(target_height)},
                   {.x = 0, .y = 0, .width = amount, .height = target_height});
      }
      return;
    }

    const int content_height = target_height - amount;
    if (shift_y > 0) {
      add_sample(
          {.x = 0,
           .y = static_cast<double>(amount),
           .width = static_cast<double>(target_width),
           .height = static_cast<double>(content_height)},
          {.x = 0, .y = 0, .width = target_width, .height = content_height});
      add_sample({.x = 0,
                  .y = static_cast<double>(target_height - 1),
                  .width = static_cast<double>(target_width),
                  .height = 1},
                 {.x = 0,
                  .y = content_height,
                  .width = target_width,
                  .height = amount});
    } else {
      add_sample({.x = 0,
                  .y = 0,
                  .width = static_cast<double>(target_width),
                  .height = static_cast<double>(content_height)},
                 {.x = 0,
                  .y = amount,
                  .width = target_width,
                  .height = content_height});
      add_sample({.x = 0,
                  .y = 0,
                  .width = static_cast<double>(target_width),
                  .height = 1},
                 {.x = 0, .y = 0, .width = target_width, .height = amount});
    }
  }

  void ClearTargets() {
    for (Target& target : targets) {
      if (target.texture != nullptr) {
        wlr_texture_destroy(target.texture);
        target.texture = nullptr;
      }
      if (target.buffer != nullptr) {
        wlr_buffer_drop(target.buffer);
        target.buffer = nullptr;
      }
    }
    target_width = 0;
    target_height = 0;
  }

  wlr_renderer* renderer = nullptr;
  wlr_allocator* allocator = nullptr;
  std::array<Target, 2> targets = {};
  int target_width = 0;
  int target_height = 0;
};

std::unique_ptr<VulkanBlurPipeline> VulkanBlurPipeline::Create(
    wlr_renderer* renderer) {
  if (renderer == nullptr || !wlr_renderer_is_vk(renderer)) {
    return nullptr;
  }
  return std::unique_ptr<VulkanBlurPipeline>(
      new VulkanBlurPipeline(std::make_unique<Impl>(renderer)));
}

VulkanBlurPipeline::VulkanBlurPipeline(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

VulkanBlurPipeline::~VulkanBlurPipeline() = default;

void VulkanBlurPipeline::SetAllocator(wlr_allocator* allocator) {
  impl_->SetAllocator(allocator);
}

bool VulkanBlurPipeline::IsSupported() const { return impl_->IsSupported(); }

wlr_texture* VulkanBlurPipeline::Render(wlr_buffer* source, float offset) {
  return impl_->Render(source, offset);
}

}  // namespace render
}  // namespace flakewm
