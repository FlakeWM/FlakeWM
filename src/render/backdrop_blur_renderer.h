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
 * Render for backdrop blur.
 */

#ifndef SRC_RENDER_BACKDROP_BLUR_RENDERER_H_
#define SRC_RENDER_BACKDROP_BLUR_RENDERER_H_

#include <memory>

#include "src/wlr_wrapper/wlroots.h"

namespace flakewm {
namespace render {

// A compositor-owned renderer proxy. It delegates every wlroots operation to
// the wrapped renderer and injects KDE backdrop blur immediately before the
// registered surface texture is drawn.
class BackdropBlurRenderer final {
 public:
  static std::unique_ptr<BackdropBlurRenderer> Create(wlr_renderer* renderer);

  ~BackdropBlurRenderer();

  BackdropBlurRenderer(const BackdropBlurRenderer&) = delete;
  BackdropBlurRenderer& operator=(const BackdropBlurRenderer&) = delete;

  wlr_renderer* Handle() const;
  bool IsSupported() const;
  bool HasActiveBlur() const;
  void SetAllocator(wlr_allocator* allocator);

  void SetSurfaceBlur(wlr_surface* surface, const pixman_region32_t* region,
                      float offset);
  void ClearSurfaceBlur(wlr_surface* surface);
  void SetTextureBlur(const void* owner, wlr_texture* texture,
                      const pixman_region32_t* region, float offset);
  void ClearTextureBlur(const void* owner);

 private:
  class Impl;

  explicit BackdropBlurRenderer(std::unique_ptr<Impl> impl);

  std::unique_ptr<Impl> impl_;
};

}  // namespace render
}  // namespace flakewm

#endif  // SRC_RENDER_BACKDROP_BLUR_RENDERER_H_
