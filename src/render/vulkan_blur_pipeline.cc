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

#include "src/wlr_wrapper/color.h"

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
#include <utility>
#include <vector>

#include "absl/log/absl_log.h"
#include "src/render/blur_kernel.h"
#include "src/render/vulkan_blur_pipeline.h"

namespace flakewm {
namespace render {
namespace {

constexpr std::array<uint32_t, 4> kBufferFormats = {
    DRM_FORMAT_ARGB8888,
    DRM_FORMAT_ABGR8888,
    DRM_FORMAT_XRGB8888,
    DRM_FORMAT_XBGR8888,
};

// A width and a height, so that a call site cannot transpose them.
struct BlurSize {
  int width;
  int height;
};

// Target chains are kept per render size, so that outputs of different sizes
// sharing this pipeline do not reallocate each other's chain every frame.
// Dropping a Vulkan render buffer waits for the whole GPU queue to go idle
// (destroy_render_buffer in wlroots), so that churn stalls the event loop.
constexpr size_t kMaxTargetSets = 4;

}  // namespace

class VulkanBlurPipeline::Impl {
 public:
  struct Target {
    wlr_buffer* buffer = nullptr;
    wlr_texture* texture = nullptr;
  };

  struct TargetSet {
    int width = 0;
    int height = 0;
    std::vector<Target> levels;
    Target scratch;
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

  wlr_texture* Render(wlr_buffer* source, float offset, int iterations) {
    if (source == nullptr || source->width <= 0 || source->height <= 0) {
      return nullptr;
    }

    const float safe_offset = std::clamp(offset, 0.001F, 64.0F);
    const BlurPlan plan = PlanBlur(SigmaForBlurOffset(safe_offset, iterations));
    const TargetSet* targets = EnsureTargets(source->width, source->height);
    if (targets == nullptr) {
      return nullptr;
    }
    const std::vector<Target>& levels = targets->levels;
    const Target& scratch = targets->scratch;

    wlr_texture* source_texture = wlr_texture_from_buffer(renderer, source);
    if (source_texture == nullptr) {
      ABSL_LOG(ERROR) << "Failed to import Vulkan blur source buffer";
      return nullptr;
    }

    // Halve the source down to the level the Gaussian runs at, blur there, and
    // expand back up.  A Gaussian is separable, so the two passes of that
    // middle step are one horizontal and one vertical convolution; the halvings
    // and expansions need no shader of their own, because a full-canvas draw
    // with a linear filter is exactly the 2x2 average on the way down and the
    // bilinear interpolation on the way back.
    const Target& coarse = levels[static_cast<size_t>(plan.levels)];
    const BlurSize coarse_size = {coarse.buffer->width, coarse.buffer->height};
    bool success = true;
    wlr_texture* finer = source_texture;
    for (int i = 1; i <= plan.levels && success; ++i) {
      const Target& destination = levels[static_cast<size_t>(i)];
      success = Blit(finer, destination.buffer);
      finer = destination.texture;
    }
    if (success) {
      wlr_texture* blur_source =
          plan.levels == 0 ? source_texture : coarse.texture;
      // The scratch is a full-resolution buffer that the coarse level borrows
      // the corner of, so the passes are told the coarse size rather than the
      // buffer's.
      success = DrawConvolution(blur_source, scratch.buffer, coarse_size,
                                plan.sigma, true) &&
                DrawConvolution(scratch.texture, coarse.buffer, coarse_size,
                                plan.sigma, false);
    }
    for (int i = plan.levels - 1; i >= 0 && success; --i) {
      success = Blit(levels[static_cast<size_t>(i) + 1].texture,
                     levels[static_cast<size_t>(i)].buffer);
    }

    wlr_texture_destroy(source_texture);
    return success ? levels[0].texture : nullptr;
  }

 private:
  // Draws `source` across the whole of `destination`, which need not be the
  // same size: a smaller destination is a 2x2 box average and a larger one a
  // bilinear expand.
  bool Blit(wlr_texture* source, wlr_buffer* destination) const {
    wlr_render_pass* pass =
        wlr_renderer_begin_buffer_pass(renderer, destination, nullptr);
    if (pass == nullptr) {
      ABSL_LOG(ERROR) << "Failed to begin Vulkan backdrop blur pass";
      return false;
    }
    const float alpha = 1.0F;
    const wlr_render_texture_options options = {
        .texture = source,
        .src_box =
            {
                .x = 0,
                .y = 0,
                .width = static_cast<double>(source->width),
                .height = static_cast<double>(source->height),
            },
        .dst_box =
            {
                .x = 0,
                .y = 0,
                .width = destination->width,
                .height = destination->height,
            },
        .alpha = &alpha,
        .filter_mode = WLR_SCALE_FILTER_BILINEAR,
        .blend_mode = WLR_RENDER_BLEND_MODE_NONE,
    };
    wlr_render_pass_add_texture(pass, &options);
    if (!wlr_render_pass_submit(pass)) {
      ABSL_LOG(ERROR) << "Failed to submit Vulkan backdrop blur pass";
      return false;
    }
    return true;
  }

  bool CreateTarget(int width, int height, const wlr_drm_format* format,
                    Target* target) {
    target->buffer =
        wlr_allocator_create_buffer(allocator, width, height, format);
    if (target->buffer == nullptr) {
      return false;
    }
    target->texture = wlr_texture_from_buffer(renderer, target->buffer);
    return target->texture != nullptr;
  }

  // Every level is allocated, not just the ones the current blur needs, and the
  // scratch stays at full resolution whatever the coarse level turns out to be.
  // Both matter: which level is coarse depends on the radius, and a blur with a
  // shallower chain than the last one must not find itself half a chain's worth
  // of geometry short.  The levels past the coarse one simply go unused, and
  // together they cost a third of one full-resolution buffer.
  bool CreateTargets(const wlr_drm_format* format, TargetSet* set) {
    for (int i = 0; i <= kBlurMaxLevels; ++i) {
      if (!CreateTarget(std::max(set->width >> i, 1),
                        std::max(set->height >> i, 1), format,
                        &set->levels.emplace_back())) {
        return false;
      }
    }
    return CreateTarget(set->width, set->height, format, &set->scratch);
  }

  // Returns the chain for this size, most recently used last.  A miss evicts
  // the least recently used chain once kMaxTargetSets are held.
  const TargetSet* EnsureTargets(int width, int height) {
    auto found =
        std::find_if(target_sets.begin(), target_sets.end(),
                     [width, height](const TargetSet& set) {
                       return set.width == width && set.height == height;
                     });
    if (found != target_sets.end()) {
      std::rotate(found, found + 1, target_sets.end());
      return &target_sets.back();
    }
    if (!IsSupported()) {
      return nullptr;
    }
    if (target_sets.size() >= kMaxTargetSets) {
      DestroyTargetSet(&target_sets.front());
      target_sets.erase(target_sets.begin());
    }

    const wlr_drm_format_set* formats = RenderFormats();
    for (uint32_t format_code : kBufferFormats) {
      const wlr_drm_format* format =
          wlr_drm_format_set_get(formats, format_code);
      if (format == nullptr) {
        continue;
      }
      TargetSet set = {.width = width, .height = height};
      if (!CreateTargets(format, &set)) {
        DestroyTargetSet(&set);
        continue;
      }
      target_sets.push_back(std::move(set));
      return &target_sets.back();
    }
    ABSL_LOG(ERROR) << "Failed to allocate Vulkan backdrop blur buffers";
    return nullptr;
  }

  const wlr_drm_format_set* RenderFormats() const {
    const wlr_renderer_impl* implementation = renderer->WLR_PRIVATE.impl;
    if (implementation == nullptr ||
        implementation->get_render_formats == nullptr) {
      return nullptr;
    }
    return implementation->get_render_formats(renderer);
  }

  // `size` is the size of the image being convolved, which is the whole of
  // `source` but only the corner of a full-resolution `destination`.  `sigma`
  // is in pixels of *that* image, so it is the plan's coarse sigma, not the one
  // the caller asked for -- the levels the source was halved through already
  // carry the rest of it.
  bool DrawConvolution(wlr_texture* source, wlr_buffer* destination,
                       BlurSize size, float sigma, bool horizontal) const {
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
                .width = size.width,
                .height = size.height,
            },
        .color = {},
        .blend_mode = WLR_RENDER_BLEND_MODE_NONE,
    };
    wlr_render_pass_add_rect(pass, &clear);

    float accumulated_weight = 0.0F;
    const std::vector<BlurTap> taps = BuildBlurTaps(sigma);
    for (const BlurTap& tap : taps) {
      accumulated_weight += tap.weight;
      const float alpha = tap.weight / accumulated_weight;
      DrawShiftedSample(pass, source, size, horizontal, tap.displacement,
                        alpha);
    }
    if (!wlr_render_pass_submit(pass)) {
      ABSL_LOG(ERROR) << "Failed to submit Vulkan backdrop blur pass";
      return false;
    }
    return true;
  }

  void DrawShiftedSample(wlr_render_pass* pass, wlr_texture* source,
                         BlurSize size, bool horizontal, float shift,
                         float alpha) const {
    const int width = size.width;
    const int height = size.height;
    const int dimension = horizontal ? width : height;
    const float magnitude =
        std::min(std::abs(shift), static_cast<float>(dimension));
    // The destination box is integral, so the shifted content is drawn across
    // whole pixels while the fractional part rides in the source box, where the
    // sampler interpolates it.  Shifting the source by `magnitude - edge` and
    // covering the remaining `edge` pixels with a stretched edge column keeps
    // the source box inside the texture and the mapping 1:1.
    const int edge =
        std::min(static_cast<int>(std::ceil(magnitude)), dimension);
    const int content = dimension - edge;

    auto add_sample = [pass, source, &alpha](const wlr_fbox& source_box,
                                             const wlr_box& destination_box) {
      if (destination_box.width <= 0 || destination_box.height <= 0) {
        return;
      }
      // The transfer function is not a choice this file gets to make.  Vulkan's
      // renderer composites in linear light: a draw de-gammas what it samples
      // (texture.frag, `pow(rgb, 2.2)`) and the pass that targets the buffer
      // re-encodes on the way out (output.frag, `pow(rgb, 1/2.2)`, selected by
      // the default GAMMA22 pass transform), so the weights below accumulate in
      // linear light and the result lands in the buffer in the same encoding it
      // was read in.  Naming GAMMA22 explicitly says the same thing the default
      // would -- wlroots treats an unset transfer function as GAMMA22 anyway --
      // and it is why this backend blurs in linear space while the GLES2 one,
      // which has no such pass, blurs in sRGB.  Each matches the compositing
      // around it; making them agree would put one of them out of step with
      // every other surface on screen.
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

    if (shift == 0.0F) {
      add_sample({.x = 0,
                  .y = 0,
                  .width = static_cast<double>(width),
                  .height = static_cast<double>(height)},
                 {.x = 0, .y = 0, .width = width, .height = height});
      return;
    }

    if (horizontal) {
      if (shift > 0.0F) {
        add_sample({.x = static_cast<double>(magnitude),
                    .y = 0,
                    .width = static_cast<double>(content),
                    .height = static_cast<double>(height)},
                   {.x = 0, .y = 0, .width = content, .height = height});
        add_sample({.x = static_cast<double>(width - 1),
                    .y = 0,
                    .width = 1,
                    .height = static_cast<double>(height)},
                   {.x = content, .y = 0, .width = edge, .height = height});
      } else {
        add_sample(
            {.x = static_cast<double>(edge) - static_cast<double>(magnitude),
             .y = 0,
             .width = static_cast<double>(content),
             .height = static_cast<double>(height)},
            {.x = edge, .y = 0, .width = content, .height = height});
        add_sample(
            {.x = 0, .y = 0, .width = 1, .height = static_cast<double>(height)},
            {.x = 0, .y = 0, .width = edge, .height = height});
      }
      return;
    }

    if (shift > 0.0F) {
      add_sample({.x = 0,
                  .y = static_cast<double>(magnitude),
                  .width = static_cast<double>(width),
                  .height = static_cast<double>(content)},
                 {.x = 0, .y = 0, .width = width, .height = content});
      add_sample({.x = 0,
                  .y = static_cast<double>(height - 1),
                  .width = static_cast<double>(width),
                  .height = 1},
                 {.x = 0, .y = content, .width = width, .height = edge});
    } else {
      add_sample(
          {.x = 0,
           .y = static_cast<double>(edge) - static_cast<double>(magnitude),
           .width = static_cast<double>(width),
           .height = static_cast<double>(content)},
          {.x = 0, .y = edge, .width = width, .height = content});
      add_sample(
          {.x = 0, .y = 0, .width = static_cast<double>(width), .height = 1},
          {.x = 0, .y = 0, .width = width, .height = edge});
    }
  }

  static void DestroyTarget(Target* target) {
    if (target->texture != nullptr) {
      wlr_texture_destroy(target->texture);
      target->texture = nullptr;
    }
    if (target->buffer != nullptr) {
      wlr_buffer_drop(target->buffer);
      target->buffer = nullptr;
    }
  }

  static void DestroyTargetSet(TargetSet* set) {
    for (Target& target : set->levels) DestroyTarget(&target);
    set->levels.clear();
    DestroyTarget(&set->scratch);
  }

  void ClearTargets() {
    for (TargetSet& set : target_sets) DestroyTargetSet(&set);
    target_sets.clear();
  }

  wlr_renderer* renderer = nullptr;
  wlr_allocator* allocator = nullptr;
  std::vector<TargetSet> target_sets;
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

wlr_texture* VulkanBlurPipeline::Render(wlr_buffer* source, float offset,
                                        int iterations) {
  return impl_->Render(source, offset, iterations);
}

}  // namespace render
}  // namespace flakewm
