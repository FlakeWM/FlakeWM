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
 * Ported from Open Kylin Wayland Compositor.
 */

#include <GLES2/gl2.h>

#include "src/wlr_wrapper/color.h"

extern "C" {
#include <wlr/render/drm_syncobj.h>
#include <wlr/render/egl.h>
#include <wlr/render/gles2.h>
#include <wlr/render/interface.h>
#include <wlr/render/pass.h>
#include <wlr/render/vulkan.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/util/transform.h>
}

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_set>
#include <utility>
#include <vector>

#include "absl/log/absl_log.h"
#include "src/render/backdrop_blur_renderer.h"
#include "src/render/blur_kernel.h"
#include "src/render/vulkan_blur_pipeline.h"
#include "src/utils/signal_listener.h"

namespace flakewm {
namespace render {
namespace {

constexpr char kBlurVertexShader[] = R"(
attribute vec2 pos;
varying vec2 texcoord;
void main() {
  gl_Position = vec4(pos.x * 2.0 - 1.0, pos.y * 2.0 - 1.0, 0.0, 1.0);
  texcoord = pos;
}
)";

constexpr char kBlurFragmentShader[] = R"(
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
varying vec2 texcoord;
uniform sampler2D image;
uniform vec2 direction;
uniform float tap_offset[64];
uniform float tap_weight[64];
uniform int tap_count;
void main() {
  vec4 color = vec4(0.0);
  for (int i = 0; i < 64; ++i) {
    if (i >= tap_count) {
      break;
    }
    color += texture2D(image, texcoord + direction * tap_offset[i]) *
             tap_weight[i];
  }
  gl_FragColor = color;
}
)";

constexpr int kCoverageSamples = 4;
constexpr int kCoverageLevels = kCoverageSamples * kCoverageSamples;

float CornerCoverage(int radius, int x, int y) {
  if (radius <= 0 || x >= radius || y >= radius) return 1.0F;
  int inside = 0;
  for (int sample_y = 0; sample_y < kCoverageSamples; ++sample_y) {
    const double py = y + (sample_y + 0.5) / kCoverageSamples - radius;
    for (int sample_x = 0; sample_x < kCoverageSamples; ++sample_x) {
      const double px = x + (sample_x + 0.5) / kCoverageSamples - radius;
      if (px * px + py * py <= radius * radius) ++inside;
    }
  }
  return static_cast<float>(inside) / kCoverageLevels;
}

GLuint CompileShader(GLenum type, const char* source) {
  const GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  GLint compiled = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
  if (compiled == GL_TRUE) {
    return shader;
  }

  GLint length = 0;
  glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
  std::vector<char> log(std::max(length, 1));
  glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr,
                     log.data());
  ABSL_LOG(ERROR) << "Failed to compile backdrop blur shader: " << log.data();
  glDeleteShader(shader);
  return 0;
}

GLuint CreateProgram() {
  const GLuint vertex = CompileShader(GL_VERTEX_SHADER, kBlurVertexShader);
  const GLuint fragment =
      CompileShader(GL_FRAGMENT_SHADER, kBlurFragmentShader);
  if (vertex == 0 || fragment == 0) {
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    return 0;
  }

  const GLuint program = glCreateProgram();
  glAttachShader(program, vertex);
  glAttachShader(program, fragment);
  glLinkProgram(program);
  glDeleteShader(vertex);
  glDeleteShader(fragment);

  GLint linked = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  if (linked == GL_TRUE) {
    return program;
  }

  GLint length = 0;
  glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
  std::vector<char> log(std::max(length, 1));
  glGetProgramInfoLog(program, static_cast<GLsizei>(log.size()), nullptr,
                      log.data());
  ABSL_LOG(ERROR) << "Failed to link backdrop blur shader: " << log.data();
  glDeleteProgram(program);
  return 0;
}

}  // namespace

class BackdropBlurRenderer::Impl {
 public:
  struct SurfaceBlur {
    SurfaceBlur(wlr_surface* new_surface, const pixman_region32_t* new_region,
                float new_offset, int new_iterations)
        : surface(new_surface), offset(new_offset), iterations(new_iterations) {
      pixman_region32_init(&region);
      pixman_region32_copy(&region, new_region);
    }

    ~SurfaceBlur() { pixman_region32_fini(&region); }

    wlr_surface* surface;
    pixman_region32_t region;
    float offset;
    int iterations;
  };

  struct TextureBlur {
    TextureBlur(const void* new_owner, wlr_texture* new_texture,
                const pixman_region32_t* new_region, float new_offset,
                int new_iterations)
        : owner(new_owner),
          texture(new_texture),
          offset(new_offset),
          iterations(new_iterations) {
      pixman_region32_init(&region);
      pixman_region32_copy(&region, new_region);
    }

    ~TextureBlur() { pixman_region32_fini(&region); }

    const void* owner;
    wlr_texture* texture;
    pixman_region32_t region;
    float offset;
    int iterations;
  };

  struct SurfaceRoundCorner {
    SurfaceRoundCorner(wlr_surface* new_surface,
                       const std::array<int, 4>& new_radii)
        : surface(new_surface), radii(new_radii), destroy(this, OnDestroy) {
      destroy.Connect(&surface->events.destroy);
    }

    static void OnDestroy(SurfaceRoundCorner* round, void*) {
      round->destroy.Disconnect();
      round->surface = nullptr;
    }

    wlr_surface* surface;
    std::array<int, 4> radii;
    utils::SignalListener<SurfaceRoundCorner, void> destroy;
  };

  struct RoundedRegions {
    RoundedRegions() {
      pixman_region32_init(&opaque);
      for (pixman_region32_t& region : partial) pixman_region32_init(&region);
    }
    ~RoundedRegions() {
      pixman_region32_fini(&opaque);
      for (pixman_region32_t& region : partial) pixman_region32_fini(&region);
    }

    RoundedRegions(const RoundedRegions&) = delete;
    RoundedRegions& operator=(const RoundedRegions&) = delete;

    pixman_region32_t opaque;
    std::array<pixman_region32_t, kCoverageLevels - 1> partial;
  };

  // One step of the blur chain: the level's image as a texture and as a draw
  // target, and a second pair for the separable Gaussian to ping-pong through
  // when this level is the coarse one.  Level 0 is the full output; every level
  // after it is half the size of the one before.
  struct GlLevel {
    GLuint textures[2] = {};
    GLuint framebuffers[2] = {};
    int width = 0;
    int height = 0;
  };

  struct RenderPass {
    wlr_render_pass base;
    Impl* renderer;
    wlr_render_pass* inner;
    wlr_buffer* target;
    std::unordered_set<wlr_surface*> rendered_blurs;
    std::unordered_set<const void*> rendered_texture_blurs;
    wlr_buffer_pass_options intermediate_options = {};
    wlr_buffer_pass_options final_options = {};
    bool deferred_completion = false;
    bool failed = false;
  };

  explicit Impl(wlr_renderer* renderer) : inner(renderer) {
    wlr_renderer_init(&base, &kRendererImplementation,
                      renderer->render_buffer_caps);
    base.color_encodings = renderer->color_encodings;
    base.features = renderer->features;
    vulkan_blur = VulkanBlurPipeline::Create(renderer);
  }

  ~Impl() = default;

  bool IsSupported() const {
    return inner != nullptr &&
           (wlr_renderer_is_gles2(inner) ||
            (vulkan_blur != nullptr && vulkan_blur->IsSupported()));
  }

  void SetAllocator(wlr_allocator* allocator) {
    if (vulkan_blur != nullptr) {
      vulkan_blur->SetAllocator(allocator);
    }
  }

  void SetSurfaceBlur(wlr_surface* surface, const pixman_region32_t* region,
                      float offset, int iterations) {
    auto found = std::find_if(
        surfaces.begin(), surfaces.end(),
        [surface](const auto& item) { return item->surface == surface; });
    if (found == surfaces.end()) {
      surfaces.push_back(
          std::make_unique<SurfaceBlur>(surface, region, offset, iterations));
      blur_changed = true;
      return;
    }
    SurfaceBlur& blur = **found;
    if (blur.offset == offset && blur.iterations == iterations &&
        pixman_region32_equal(&blur.region, region)) {
      return;
    }
    pixman_region32_copy(&blur.region, region);
    blur.offset = offset;
    blur.iterations = iterations;
    blur_changed = true;
  }

  void ClearSurfaceBlur(wlr_surface* surface) {
    if (std::erase_if(surfaces, [surface](const auto& item) {
          return item->surface == surface;
        }) != 0) {
      blur_changed = true;
    }
  }

  void SetSurfaceRoundCorner(wlr_surface* surface,
                             const std::array<int, 4>& radii) {
    std::erase_if(round_corners,
                  [](const auto& item) { return item->surface == nullptr; });
    auto found = std::find_if(
        round_corners.begin(), round_corners.end(),
        [surface](const auto& item) { return item->surface == surface; });
    if (std::all_of(radii.begin(), radii.end(),
                    [](int radius) { return radius <= 0; })) {
      if (found != round_corners.end()) round_corners.erase(found);
      return;
    }
    if (found == round_corners.end()) {
      round_corners.push_back(
          std::make_unique<SurfaceRoundCorner>(surface, radii));
    } else {
      (*found)->radii = radii;
    }
  }

  void ClearSurfaceRoundCorner(wlr_surface* surface) {
    std::erase_if(round_corners, [surface](const auto& item) {
      return item->surface == nullptr || item->surface == surface;
    });
  }

  void SetTextureBlur(const void* owner, wlr_texture* texture,
                      const pixman_region32_t* region, float offset,
                      int iterations) {
    auto found = std::find_if(
        textures_blur.begin(), textures_blur.end(),
        [owner](const auto& item) { return item->owner == owner; });
    if (found == textures_blur.end()) {
      textures_blur.push_back(std::make_unique<TextureBlur>(
          owner, texture, region, offset, iterations));
      blur_changed = true;
      return;
    }
    TextureBlur& blur = **found;
    if (blur.texture == texture && blur.offset == offset &&
        blur.iterations == iterations &&
        pixman_region32_equal(&blur.region, region)) {
      return;
    }
    blur.texture = texture;
    pixman_region32_copy(&blur.region, region);
    blur.offset = offset;
    blur.iterations = iterations;
    blur_changed = true;
  }

  void ClearTextureBlur(const void* owner) {
    if (std::erase_if(textures_blur, [owner](const auto& item) {
          return item->owner == owner;
        }) != 0) {
      blur_changed = true;
    }
  }

 private:
  static Impl* FromRenderer(wlr_renderer* renderer) {
    return reinterpret_cast<Impl*>(renderer);
  }

  static RenderPass* FromPass(wlr_render_pass* pass) {
    return reinterpret_cast<RenderPass*>(pass);
  }

  static const wlr_drm_format_set* GetTextureFormats(wlr_renderer* renderer,
                                                     uint32_t caps) {
    return wlr_renderer_get_texture_formats(FromRenderer(renderer)->inner,
                                            caps);
  }

  static const wlr_drm_format_set* GetRenderFormats(wlr_renderer* renderer) {
    Impl* self = FromRenderer(renderer);
    const wlr_renderer_impl* implementation = self->inner->WLR_PRIVATE.impl;
    return implementation->get_render_formats == nullptr
               ? nullptr
               : implementation->get_render_formats(self->inner);
  }

  static int GetDrmFd(wlr_renderer* renderer) {
    return wlr_renderer_get_drm_fd(FromRenderer(renderer)->inner);
  }

  static wlr_texture* TextureFromBuffer(wlr_renderer* renderer,
                                        wlr_buffer* buffer) {
    return wlr_texture_from_buffer(FromRenderer(renderer)->inner, buffer);
  }

  static wlr_render_pass* BeginBufferPass(
      wlr_renderer* renderer, wlr_buffer* buffer,
      const wlr_buffer_pass_options* options) {
    Impl* self = FromRenderer(renderer);
    const bool defer_completion =
        self->vulkan_blur != nullptr &&
        (!self->surfaces.empty() || !self->textures_blur.empty()) &&
        options != nullptr &&
        (options->timer != nullptr || options->signal_timeline != nullptr);
    auto* pass = new RenderPass{
        .renderer = self,
        .target = buffer,
        .deferred_completion = defer_completion,
    };
    if (options != nullptr) {
      pass->final_options = *options;
      pass->intermediate_options = *options;
      pass->intermediate_options.timer = nullptr;
      pass->intermediate_options.signal_timeline = nullptr;
      pass->intermediate_options.signal_point = 0;
      if (pass->final_options.color_transform != nullptr) {
        wlr_color_transform_ref(pass->final_options.color_transform);
      }
      if (pass->final_options.signal_timeline != nullptr) {
        wlr_drm_syncobj_timeline_ref(pass->final_options.signal_timeline);
      }
    }
    const wlr_buffer_pass_options* begin_options =
        defer_completion ? &pass->intermediate_options : options;
    wlr_render_pass* inner_pass =
        wlr_renderer_begin_buffer_pass(self->inner, buffer, begin_options);
    if (inner_pass == nullptr) {
      self->ReleasePassOptions(pass);
      delete pass;
      return nullptr;
    }
    pass->inner = inner_pass;
    wlr_render_pass_init(&pass->base, &kPassImplementation);
    return &pass->base;
  }

  static wlr_render_timer* CreateRenderTimer(wlr_renderer* renderer) {
    return wlr_render_timer_create(FromRenderer(renderer)->inner);
  }

  static void DestroyRenderer(wlr_renderer* renderer) {
    Impl* self = FromRenderer(renderer);
    self->DestroyGlResources();
    self->vulkan_blur.reset();
    wlr_renderer_destroy(std::exchange(self->inner, nullptr));
  }

  static bool SubmitPass(wlr_render_pass* pass) {
    RenderPass* self = FromPass(pass);
    bool result = !self->failed && self->inner != nullptr &&
                  wlr_render_pass_submit(self->inner);
    self->inner = nullptr;
    if (result && self->deferred_completion) {
      wlr_render_pass* completion = wlr_renderer_begin_buffer_pass(
          self->renderer->inner, self->target, &self->final_options);
      result = completion != nullptr && wlr_render_pass_submit(completion);
    }
    self->renderer->ReleasePassOptions(self);
    delete self;
    return result;
  }

  static void AddTexture(wlr_render_pass* pass,
                         const wlr_render_texture_options* options) {
    RenderPass* self = FromPass(pass);
    if (self->failed || self->inner == nullptr) {
      return;
    }
    const SurfaceRoundCorner* round = self->renderer->RoundCornerFor(options);
    if (round == nullptr) {
      const bool force_blending =
          self->renderer->ApplySurfaceBlur(self, options);
      if (!self->failed && self->inner != nullptr) {
        if (force_blending) {
          wlr_render_texture_options blended = *options;
          blended.blend_mode = WLR_RENDER_BLEND_MODE_PREMULTIPLIED;
          wlr_render_pass_add_texture(self->inner, &blended);
        } else {
          wlr_render_pass_add_texture(self->inner, options);
        }
      }
      return;
    }

    self->renderer->AddRoundedSurfaceTexture(self, options, *round);
  }

  static void AddRect(wlr_render_pass* pass,
                      const wlr_render_rect_options* options) {
    RenderPass* self = FromPass(pass);
    if (!self->failed && self->inner != nullptr) {
      wlr_render_pass_add_rect(self->inner, options);
    }
  }

  void ReleasePassOptions(RenderPass* pass) const {
    if (pass->final_options.color_transform != nullptr) {
      wlr_color_transform_unref(pass->final_options.color_transform);
      pass->final_options.color_transform = nullptr;
    }
    if (pass->final_options.signal_timeline != nullptr) {
      wlr_drm_syncobj_timeline_unref(pass->final_options.signal_timeline);
      pass->final_options.signal_timeline = nullptr;
    }
  }

  const SurfaceRoundCorner* RoundCornerFor(
      const wlr_render_texture_options* options) const {
    auto found = std::find_if(round_corners.begin(), round_corners.end(),
                              [options](const auto& item) {
                                return item->surface != nullptr &&
                                       wlr_surface_get_texture(item->surface) ==
                                           options->texture;
                              });
    return found == round_corners.end() ? nullptr : found->get();
  }

  static void IntersectClip(pixman_region32_t* region,
                            const pixman_region32_t* clip) {
    if (clip != nullptr) pixman_region32_intersect(region, region, clip);
  }

  static std::array<int, 4> ScaledRadii(const SurfaceRoundCorner& round,
                                        const wlr_box& destination) {
    const int surface_width = std::max(round.surface->current.width, 1);
    const int surface_height = std::max(round.surface->current.height, 1);
    const double scale =
        std::min(static_cast<double>(destination.width) / surface_width,
                 static_cast<double>(destination.height) / surface_height);
    std::array<int, 4> result = {};
    const int limit =
        std::max(0, std::min(destination.width, destination.height) / 2);
    for (size_t i = 0; i < result.size(); ++i) {
      result[i] = std::clamp(
          static_cast<int>(std::lround(round.radii[i] * scale)), 0, limit);
    }
    return result;
  }

  static void BuildRoundedRegions(const wlr_box& destination,
                                  const std::array<int, 4>& radii,
                                  const pixman_region32_t* clip,
                                  RoundedRegions* regions) {
    if (destination.width <= 0 || destination.height <= 0) return;

    for (int row = 0; row < destination.height; ++row) {
      const bool top = row < std::max(radii[0], radii[1]);
      const bool bottom =
          destination.height - row - 1 < std::max(radii[3], radii[2]);
      const int left_radius = top ? radii[0] : (bottom ? radii[3] : 0);
      const int right_radius = top ? radii[1] : (bottom ? radii[2] : 0);
      const int corner_y = top ? row : destination.height - row - 1;

      int left_opaque = 0;
      for (int x = 0; x < left_radius; ++x) {
        const float coverage = CornerCoverage(left_radius, x, corner_y);
        const int level = std::clamp(
            static_cast<int>(std::lround(coverage * kCoverageLevels)), 0,
            kCoverageLevels);
        if (level == kCoverageLevels) {
          left_opaque = x;
          break;
        }
        left_opaque = x + 1;
        if (level > 0) {
          pixman_region32_union_rect(
              &regions->partial[level - 1], &regions->partial[level - 1],
              destination.x + x, destination.y + row, 1, 1);
        }
      }

      int right_opaque = 0;
      for (int x = 0; x < right_radius; ++x) {
        const float coverage = CornerCoverage(right_radius, x, corner_y);
        const int level = std::clamp(
            static_cast<int>(std::lround(coverage * kCoverageLevels)), 0,
            kCoverageLevels);
        if (level == kCoverageLevels) {
          right_opaque = x;
          break;
        }
        right_opaque = x + 1;
        if (level > 0) {
          pixman_region32_union_rect(&regions->partial[level - 1],
                                     &regions->partial[level - 1],
                                     destination.x + destination.width - x - 1,
                                     destination.y + row, 1, 1);
        }
      }

      const int width = destination.width - left_opaque - right_opaque;
      if (width > 0) {
        pixman_region32_union_rect(&regions->opaque, &regions->opaque,
                                   destination.x + left_opaque,
                                   destination.y + row, width, 1);
      }
    }

    IntersectClip(&regions->opaque, clip);
    for (pixman_region32_t& region : regions->partial) {
      IntersectClip(&region, clip);
    }
  }

  void AddRoundedSurfaceTexture(RenderPass* pass,
                                const wlr_render_texture_options* options,
                                const SurfaceRoundCorner& round) {
    wlr_box destination = {};
    wlr_render_texture_options_get_dst_box(options, &destination);
    const std::array<int, 4> scaled = ScaledRadii(round, destination);
    RoundedRegions regions;
    BuildRoundedRegions(destination, scaled, options->clip, &regions);

    wlr_render_texture_options rounded = *options;
    rounded.clip = &regions.opaque;
    const bool force_blending = ApplySurfaceBlur(pass, &rounded);
    if (pass->failed || pass->inner == nullptr) return;

    rounded.blend_mode = WLR_RENDER_BLEND_MODE_PREMULTIPLIED;
    if (!pixman_region32_empty(&regions.opaque)) {
      wlr_render_pass_add_texture(pass->inner, &rounded);
    }

    const float original_alpha = wlr_render_texture_options_get_alpha(options);
    for (size_t i = 0; i < regions.partial.size(); ++i) {
      if (pixman_region32_empty(&regions.partial[i])) continue;
      const float alpha =
          original_alpha * static_cast<float>(i + 1) / kCoverageLevels;
      rounded.alpha = &alpha;
      rounded.clip = &regions.partial[i];
      // A rounded edge always needs blending, including RGBX/opaque buffers.
      rounded.blend_mode = WLR_RENDER_BLEND_MODE_PREMULTIPLIED;
      wlr_render_pass_add_texture(pass->inner, &rounded);
    }
    (void)force_blending;
  }

  static bool CreateGlTarget(int width, int height, GlLevel* level) {
    level->width = width;
    level->height = height;
    glGenTextures(2, level->textures);
    glGenFramebuffers(2, level->framebuffers);
    for (size_t i = 0; i < 2; ++i) {
      glBindTexture(GL_TEXTURE_2D, level->textures[i]);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
                   GL_UNSIGNED_BYTE, nullptr);
      glBindFramebuffer(GL_FRAMEBUFFER, level->framebuffers[i]);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_TEXTURE_2D, level->textures[i], 0);
      if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        ABSL_LOG(ERROR) << "Failed to allocate backdrop blur framebuffer";
        DestroyGlTarget(level);
        return false;
      }
    }
    return true;
  }

  static void DestroyGlTarget(GlLevel* level) {
    glDeleteFramebuffers(2, level->framebuffers);
    glDeleteTextures(2, level->textures);
    *level = {};
  }

  void ReleaseGlTargets() {
    for (GlLevel& level : levels) DestroyGlTarget(&level);
    levels.clear();
    target_width = target_height = 0;
  }

  bool EnsureGlResources(int width, int height) {
    if (program == 0) {
      program = CreateProgram();
      if (program == 0) {
        return false;
      }
      image_uniform = glGetUniformLocation(program, "image");
      direction_uniform = glGetUniformLocation(program, "direction");
      tap_offset_uniform = glGetUniformLocation(program, "tap_offset");
      tap_weight_uniform = glGetUniformLocation(program, "tap_weight");
      tap_count_uniform = glGetUniformLocation(program, "tap_count");
      position_attribute = glGetAttribLocation(program, "pos");
      if (image_uniform < 0 || direction_uniform < 0 ||
          tap_offset_uniform < 0 || tap_weight_uniform < 0 ||
          tap_count_uniform < 0) {
        ABSL_LOG(ERROR) << "Backdrop blur shader is missing its uniforms";
        return false;
      }
    }

    if (target_width == width && target_height == height && !levels.empty()) {
      return true;
    }

    ReleaseGlTargets();
    // Every level is allocated, not just the ones the current blur needs: which
    // level ends up coarse depends on the radius, and a blur with a shallower
    // chain than the last one must not find itself half a chain's worth of
    // geometry short.  The levels past the coarse one simply go unused, and
    // together they cost a third of one full-resolution buffer.
    for (int i = 0; i <= kBlurMaxLevels; ++i) {
      GlLevel level;
      if (!CreateGlTarget(std::max(width >> i, 1), std::max(height >> i, 1),
                          &level)) {
        ReleaseGlTargets();
        return false;
      }
      levels.push_back(level);
    }
    target_width = width;
    target_height = height;
    return true;
  }

  // `texture` is sampled across the whole of `framebuffer`, which is `width` by
  // `height`.  Passing a destination smaller than the source is a 2x2 box
  // average and a larger one is a bilinear expand -- both are what a single
  // full-canvas tap does with a linear filter, so the chain needs no shader of
  // its own.  `clip` is in destination pixels and must be null for those.
  void DrawTexture(GLuint texture, GLuint framebuffer, int width, int height,
                   float direction_x, float direction_y,
                   const pixman_region32_t* clip,
                   const std::vector<BlurTap>& taps) const {
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glViewport(0, 0, width, height);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glUseProgram(program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(image_uniform, 0);
    glUniform2f(direction_uniform, direction_x, direction_y);
    const GLsizei count = static_cast<GLsizei>(
        std::min(taps.size(), static_cast<size_t>(kBlurMaxTaps)));
    std::array<GLfloat, kBlurMaxTaps> offsets = {};
    std::array<GLfloat, kBlurMaxTaps> weights = {};
    for (GLsizei i = 0; i < count; ++i) {
      offsets[static_cast<size_t>(i)] =
          taps[static_cast<size_t>(i)].displacement;
      weights[static_cast<size_t>(i)] = taps[static_cast<size_t>(i)].weight;
    }
    glUniform1i(tap_count_uniform, count);
    if (count > 0) {
      glUniform1fv(tap_offset_uniform, count, offsets.data());
      glUniform1fv(tap_weight_uniform, count, weights.data());
    }
    glEnableVertexAttribArray(position_attribute);

    pixman_region32_t full;
    if (clip == nullptr) {
      pixman_region32_init_rect(&full, 0, 0, width, height);
      clip = &full;
    }
    int rectangle_count = 0;
    const pixman_box32_t* rectangles =
        pixman_region32_rectangles(clip, &rectangle_count);
    std::vector<GLfloat> vertices;
    vertices.reserve(static_cast<size_t>(rectangle_count) * 12);
    for (int i = 0; i < rectangle_count; ++i) {
      const float x1 = static_cast<float>(rectangles[i].x1) / width;
      const float y1 = static_cast<float>(rectangles[i].y1) / height;
      const float x2 = static_cast<float>(rectangles[i].x2) / width;
      const float y2 = static_cast<float>(rectangles[i].y2) / height;
      vertices.insert(vertices.end(),
                      {x1, y1, x2, y1, x1, y2, x2, y1, x2, y2, x1, y2});
    }
    if (!vertices.empty()) {
      glVertexAttribPointer(position_attribute, 2, GL_FLOAT, GL_FALSE, 0,
                            vertices.data());
      glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size() / 2));
    }
    glDisableVertexAttribArray(position_attribute);
    if (clip == &full) {
      pixman_region32_fini(&full);
    }
  }

  pixman_region32_t SurfaceRegion(const SurfaceBlur& blur,
                                  const wlr_render_texture_options& options,
                                  int target_width, int target_height) const {
    pixman_region32_t result;
    pixman_region32_init(&result);
    const wlr_box destination = options.dst_box;
    if (pixman_region32_empty(&blur.region)) {
      pixman_region32_union_rect(&result, &result, destination.x, destination.y,
                                 destination.width, destination.height);
    } else {
      const wl_output_transform output_transform = wlr_output_transform_compose(
          blur.surface->current.transform, options.transform);
      int untransformed_width = destination.width;
      int untransformed_height = destination.height;
      if ((output_transform & WL_OUTPUT_TRANSFORM_90) != 0) {
        std::swap(untransformed_width, untransformed_height);
      }
      const int surface_width = std::max(blur.surface->current.width, 1);
      const int surface_height = std::max(blur.surface->current.height, 1);
      pixman_region32_t bounded_region;
      pixman_region32_init_rect(&bounded_region, 0, 0, surface_width,
                                surface_height);
      pixman_region32_intersect(&bounded_region, &bounded_region, &blur.region);
      int rectangle_count = 0;
      const pixman_box32_t* rectangles =
          pixman_region32_rectangles(&bounded_region, &rectangle_count);
      for (int i = 0; i < rectangle_count; ++i) {
        const wlr_box local = {
            .x = static_cast<int>(
                std::floor(static_cast<double>(rectangles[i].x1) *
                           untransformed_width / surface_width)),
            .y = static_cast<int>(
                std::floor(static_cast<double>(rectangles[i].y1) *
                           untransformed_height / surface_height)),
            .width = static_cast<int>(
                         std::ceil(static_cast<double>(rectangles[i].x2) *
                                   untransformed_width / surface_width)) -
                     static_cast<int>(
                         std::floor(static_cast<double>(rectangles[i].x1) *
                                    untransformed_width / surface_width)),
            .height = static_cast<int>(
                          std::ceil(static_cast<double>(rectangles[i].y2) *
                                    untransformed_height / surface_height)) -
                      static_cast<int>(
                          std::floor(static_cast<double>(rectangles[i].y1) *
                                     untransformed_height / surface_height)),
        };
        wlr_box transformed = {};
        wlr_box_transform(&transformed, &local, output_transform,
                          untransformed_width, untransformed_height);
        pixman_region32_union_rect(&result, &result,
                                   destination.x + transformed.x,
                                   destination.y + transformed.y,
                                   transformed.width, transformed.height);
      }
      pixman_region32_fini(&bounded_region);
    }
    pixman_region32_intersect_rect(&result, &result, 0, 0, target_width,
                                   target_height);
    if (options.clip != nullptr) {
      pixman_region32_intersect(&result, &result, options.clip);
    }
    return result;
  }

  pixman_region32_t TextureRegion(const TextureBlur& blur,
                                  const wlr_render_texture_options& options,
                                  int target_width, int target_height) const {
    pixman_region32_t result;
    pixman_region32_init(&result);
    const wlr_box destination = options.dst_box;
    if (pixman_region32_empty(&blur.region)) {
      pixman_region32_union_rect(&result, &result, destination.x, destination.y,
                                 destination.width, destination.height);
    } else {
      const int texture_width =
          std::max(static_cast<int>(blur.texture->width), 1);
      const int texture_height =
          std::max(static_cast<int>(blur.texture->height), 1);
      pixman_region32_t bounded_region;
      pixman_region32_init_rect(&bounded_region, 0, 0, texture_width,
                                texture_height);
      pixman_region32_intersect(&bounded_region, &bounded_region, &blur.region);
      int rectangle_count = 0;
      const pixman_box32_t* rectangles =
          pixman_region32_rectangles(&bounded_region, &rectangle_count);
      for (int i = 0; i < rectangle_count; ++i) {
        const int x1 =
            destination.x +
            static_cast<int>(std::floor(static_cast<double>(rectangles[i].x1) *
                                        destination.width / texture_width));
        const int y1 =
            destination.y +
            static_cast<int>(std::floor(static_cast<double>(rectangles[i].y1) *
                                        destination.height / texture_height));
        const int x2 =
            destination.x +
            static_cast<int>(std::ceil(static_cast<double>(rectangles[i].x2) *
                                       destination.width / texture_width));
        const int y2 =
            destination.y +
            static_cast<int>(std::ceil(static_cast<double>(rectangles[i].y2) *
                                       destination.height / texture_height));
        pixman_region32_union_rect(&result, &result, x1, y1,
                                   std::max(0, x2 - x1), std::max(0, y2 - y1));
      }
      pixman_region32_fini(&bounded_region);
    }
    pixman_region32_intersect_rect(&result, &result, 0, 0, target_width,
                                   target_height);
    if (options.clip != nullptr) {
      pixman_region32_intersect(&result, &result, options.clip);
    }
    return result;
  }

  void ApplyRegionBlur(RenderPass* pass, pixman_region32_t* clip, float offset,
                       int iterations) {
    if (pixman_region32_empty(clip)) return;

    if (vulkan_blur != nullptr) {
      const bool submitted = wlr_render_pass_submit(pass->inner);
      pass->inner = nullptr;
      if (!submitted) {
        pass->failed = true;
        return;
      }

      wlr_texture* blurred =
          vulkan_blur->Render(pass->target, offset, iterations);
      pass->inner = wlr_renderer_begin_buffer_pass(inner, pass->target,
                                                   &pass->intermediate_options);
      if (pass->inner == nullptr) {
        pass->failed = true;
        return;
      }
      if (blurred != nullptr) {
        // `blurred` is a level-0 buffer that a Vulkan pass wrote, so it holds
        // the same encoding as everything else the Vulkan renderer produces and
        // this draw undoes and redoes that encoding exactly as any other
        // texture draw does -- see the note on DrawShiftedSample in
        // vulkan_blur_pipeline.cc.  The transfer function here is not a
        // separate choice; it matches the pass this lands in.
        const wlr_render_texture_options blur_options = {
            .texture = blurred,
            .dst_box =
                {
                    .x = 0,
                    .y = 0,
                    .width = pass->target->width,
                    .height = pass->target->height,
                },
            .clip = clip,
            .filter_mode = WLR_SCALE_FILTER_BILINEAR,
            .blend_mode = WLR_RENDER_BLEND_MODE_NONE,
            .transfer_function = WLR_COLOR_TRANSFER_FUNCTION_GAMMA22,
        };
        wlr_render_pass_add_texture(pass->inner, &blur_options);
      }
      return;
    }

    const float safe_offset = std::clamp(offset, 0.001F, 64.0F);
    const BlurPlan plan = PlanBlur(SigmaForBlurOffset(safe_offset, iterations));

    GLint output_framebuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &output_framebuffer);
    if (!EnsureGlResources(pass->target->width, pass->target->height)) {
      glBindFramebuffer(GL_FRAMEBUFFER, output_framebuffer);
      return;
    }

    // A tap that is not displaced resamples the source at the destination's
    // pixel centres; for the halvings and expansions of the chain that is all
    // the shader has to do.
    const std::vector<BlurTap> resample = {{0.0F, 1.0F}};

    glBindFramebuffer(GL_FRAMEBUFFER, output_framebuffer);
    glBindTexture(GL_TEXTURE_2D, levels[0].textures[0]);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, target_width,
                        target_height);

    for (int i = 0; i < plan.levels; ++i) {
      const GlLevel& source = levels[static_cast<size_t>(i)];
      const GlLevel& destination = levels[static_cast<size_t>(i) + 1];
      DrawTexture(source.textures[0], destination.framebuffers[0],
                  destination.width, destination.height, 0.0F, 0.0F, nullptr,
                  resample);
    }

    // One horizontal and one vertical pass at the coarse level: this is the
    // only step whose tap count grows with the radius, and it now runs over a
    // fraction of the pixels.  `direction` converts the pixel displacements to
    // texture coordinates.
    const GlLevel& coarse = levels[static_cast<size_t>(plan.levels)];
    const std::vector<BlurTap> taps = BuildBlurTaps(plan.sigma);
    DrawTexture(coarse.textures[0], coarse.framebuffers[1], coarse.width,
                coarse.height, 1.0F / static_cast<float>(coarse.width), 0.0F,
                nullptr, taps);
    DrawTexture(coarse.textures[1], coarse.framebuffers[0], coarse.width,
                coarse.height, 0.0F, 1.0F / static_cast<float>(coarse.height),
                nullptr, taps);

    for (int i = plan.levels - 1; i >= 0; --i) {
      const GlLevel& source = levels[static_cast<size_t>(i) + 1];
      const GlLevel& destination = levels[static_cast<size_t>(i)];
      DrawTexture(source.textures[0], destination.framebuffers[0],
                  destination.width, destination.height, 0.0F, 0.0F, nullptr,
                  resample);
    }

    DrawTexture(levels[0].textures[0], static_cast<GLuint>(output_framebuffer),
                target_width, target_height, 0.0F, 0.0F, clip, resample);
    glBindFramebuffer(GL_FRAMEBUFFER, output_framebuffer);
    glViewport(0, 0, target_width, target_height);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
  }

  bool ApplySurfaceBlur(RenderPass* pass,
                        const wlr_render_texture_options* options) {
    if (pass->target->width <= 0 || pass->target->height <= 0) return false;
    auto found = std::find_if(
        surfaces.begin(), surfaces.end(), [options](const auto& item) {
          return wlr_surface_get_texture(item->surface) == options->texture;
        });
    if (found != surfaces.end()) {
      SurfaceBlur& blur = **found;
      if (pass->rendered_blurs.insert(blur.surface).second) {
        pixman_region32_t clip = SurfaceRegion(
            blur, *options, pass->target->width, pass->target->height);
        ApplyRegionBlur(pass, &clip, blur.offset, blur.iterations);
        pixman_region32_fini(&clip);
      }
      // DTK surfaces can advertise their full buffer as opaque despite using
      // ARGB translucency.  GXWM deliberately blends them for blur contexts.
      return true;
    }

    auto texture = std::find_if(
        textures_blur.begin(), textures_blur.end(),
        [options, pass](const auto& item) {
          return !pass->rendered_texture_blurs.contains(item->owner) &&
                 item->texture == options->texture;
        });
    if (texture == textures_blur.end()) return false;
    TextureBlur& blur = **texture;
    pass->rendered_texture_blurs.insert(blur.owner);
    pixman_region32_t clip = TextureRegion(blur, *options, pass->target->width,
                                           pass->target->height);
    ApplyRegionBlur(pass, &clip, blur.offset, blur.iterations);
    pixman_region32_fini(&clip);
    return false;
  }

  void DestroyGlResources() {
    if (inner == nullptr || !wlr_renderer_is_gles2(inner)) {
      return;
    }
    wlr_egl* egl = wlr_gles2_renderer_get_egl(inner);
    const EGLDisplay previous_display = eglGetCurrentDisplay();
    const EGLSurface previous_draw = eglGetCurrentSurface(EGL_DRAW);
    const EGLSurface previous_read = eglGetCurrentSurface(EGL_READ);
    const EGLContext previous_context = eglGetCurrentContext();
    const EGLDisplay display = wlr_egl_get_display(egl);
    if (eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                       wlr_egl_get_context(egl)) == EGL_TRUE) {
      glDeleteProgram(program);
      ReleaseGlTargets();
      if (previous_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(previous_display, previous_draw, previous_read,
                       previous_context);
      } else {
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
      }
    }
    program = 0;
  }

  wlr_renderer base = {};
  wlr_renderer* inner = nullptr;
  std::unique_ptr<VulkanBlurPipeline> vulkan_blur;
  std::vector<std::unique_ptr<SurfaceBlur>> surfaces;
  std::vector<std::unique_ptr<SurfaceRoundCorner>> round_corners;
  std::vector<std::unique_ptr<TextureBlur>> textures_blur;
  // Set when a blur registration actually changes, so that re-registering an
  // unchanged blur on every commit does not repaint every output.
  bool blur_changed = false;
  GLuint program = 0;
  GLint image_uniform = -1;
  GLint direction_uniform = -1;
  GLint tap_offset_uniform = -1;
  GLint tap_weight_uniform = -1;
  GLint tap_count_uniform = -1;
  GLint position_attribute = -1;
  std::vector<GlLevel> levels;
  int target_width = 0;
  int target_height = 0;

  static const wlr_renderer_impl kRendererImplementation;
  static const wlr_render_pass_impl kPassImplementation;

  friend class BackdropBlurRenderer;
};

const wlr_renderer_impl BackdropBlurRenderer::Impl::kRendererImplementation = {
    .get_texture_formats = GetTextureFormats,
    .get_render_formats = GetRenderFormats,
    .destroy = DestroyRenderer,
    .get_drm_fd = GetDrmFd,
    .texture_from_buffer = TextureFromBuffer,
    .begin_buffer_pass = BeginBufferPass,
    .render_timer_create = CreateRenderTimer,
};

const wlr_render_pass_impl BackdropBlurRenderer::Impl::kPassImplementation = {
    .submit = SubmitPass,
    .add_texture = AddTexture,
    .add_rect = AddRect,
};

std::unique_ptr<BackdropBlurRenderer> BackdropBlurRenderer::Create(
    wlr_renderer* renderer) {
  if (renderer == nullptr ||
      (!wlr_renderer_is_gles2(renderer) && !wlr_renderer_is_vk(renderer))) {
    return nullptr;
  }
  return std::unique_ptr<BackdropBlurRenderer>(
      new BackdropBlurRenderer(std::make_unique<Impl>(renderer)));
}

BackdropBlurRenderer::BackdropBlurRenderer(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

BackdropBlurRenderer::~BackdropBlurRenderer() = default;

wlr_renderer* BackdropBlurRenderer::Handle() const { return &impl_->base; }

bool BackdropBlurRenderer::IsSupported() const { return impl_->IsSupported(); }

bool BackdropBlurRenderer::HasActiveBlur() const {
  return !impl_->surfaces.empty() || !impl_->textures_blur.empty();
}

bool BackdropBlurRenderer::HasRoundedCorners() const {
  return !impl_->round_corners.empty();
}

bool BackdropBlurRenderer::TakeBlurChanged() {
  return std::exchange(impl_->blur_changed, false);
}

void BackdropBlurRenderer::SetAllocator(wlr_allocator* allocator) {
  impl_->SetAllocator(allocator);
}

void BackdropBlurRenderer::SetSurfaceBlur(wlr_surface* surface,
                                          const pixman_region32_t* region,
                                          float offset, int iterations) {
  if (surface != nullptr && region != nullptr) {
    impl_->SetSurfaceBlur(surface, region, offset, iterations);
  }
}

void BackdropBlurRenderer::ClearSurfaceBlur(wlr_surface* surface) {
  impl_->ClearSurfaceBlur(surface);
}

void BackdropBlurRenderer::SetSurfaceRoundCorner(
    wlr_surface* surface, const std::array<int, 4>& radii) {
  if (surface != nullptr) impl_->SetSurfaceRoundCorner(surface, radii);
}

void BackdropBlurRenderer::ClearSurfaceRoundCorner(wlr_surface* surface) {
  if (surface != nullptr) impl_->ClearSurfaceRoundCorner(surface);
}

void BackdropBlurRenderer::SetTextureBlur(const void* owner,
                                          wlr_texture* texture,
                                          const pixman_region32_t* region,
                                          float offset, int iterations) {
  if (owner != nullptr && texture != nullptr && region != nullptr) {
    impl_->SetTextureBlur(owner, texture, region, offset, iterations);
  }
}

void BackdropBlurRenderer::ClearTextureBlur(const void* owner) {
  impl_->ClearTextureBlur(owner);
}

}  // namespace render
}  // namespace flakewm
