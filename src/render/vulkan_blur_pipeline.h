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
 */

#ifndef SRC_RENDER_VULKAN_BLUR_PIPELINE_H_
#define SRC_RENDER_VULKAN_BLUR_PIPELINE_H_

#include <memory>

#include "src/wlr_wrapper/wlroots.h"

namespace flakewm {
namespace render {

// Vulkan backdrop blur owns no Vulkan command buffers/wlroots private objects.
class VulkanBlurPipeline final {
 public:
  static std::unique_ptr<VulkanBlurPipeline> Create(wlr_renderer* renderer);

  ~VulkanBlurPipeline();

  VulkanBlurPipeline(const VulkanBlurPipeline&) = delete;
  VulkanBlurPipeline& operator=(const VulkanBlurPipeline&) = delete;

  void SetAllocator(wlr_allocator* allocator);
  bool IsSupported() const;

  // The returned texture remains owned by this pipeline and is valid until
  // the allocator changes, the render size changes, or the pipeline dies.
  wlr_texture* Render(wlr_buffer* source, float offset, int iterations);

 private:
  class Impl;

  explicit VulkanBlurPipeline(std::unique_ptr<Impl> impl);

  std::unique_ptr<Impl> impl_;
};

}  // namespace render
}  // namespace flakewm

#endif  // SRC_RENDER_VULKAN_BLUR_PIPELINE_H_
