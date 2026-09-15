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
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_set>
#include <utility>
#include <vector>

#include "absl/log/absl_log.h"
#include "src/render/backdrop_blur_renderer.h"
#include "src/render/vulkan_blur_pipeline.h"

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
void main() {
  vec4 color = texture2D(image, texcoord) * 0.2270270270;
  color += texture2D(image, texcoord + direction * 1.3846153846) * 0.3162162162;
  color += texture2D(image, texcoord - direction * 1.3846153846) * 0.3162162162;
  color += texture2D(image, texcoord + direction * 3.2307692308) * 0.0702702703;
  color += texture2D(image, texcoord - direction * 3.2307692308) * 0.0702702703;
  gl_FragColor = color;
}
)";

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
                float new_offset)
        : surface(new_surface), offset(new_offset) {
      pixman_region32_init(&region);
      pixman_region32_copy(&region, new_region);
    }

    ~SurfaceBlur() { pixman_region32_fini(&region); }

    wlr_surface* surface;
    pixman_region32_t region;
    float offset;
  };

  struct TextureBlur {
    TextureBlur(const void* new_owner, wlr_texture* new_texture,
                const pixman_region32_t* new_region, float new_offset)
        : owner(new_owner), texture(new_texture), offset(new_offset) {
      pixman_region32_init(&region);
      pixman_region32_copy(&region, new_region);
    }

    ~TextureBlur() { pixman_region32_fini(&region); }

    const void* owner;
    wlr_texture* texture;
    pixman_region32_t region;
    float offset;
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
                      float offset) {
    auto found = std::find_if(
        surfaces.begin(), surfaces.end(),
        [surface](const auto& item) { return item->surface == surface; });
    if (found == surfaces.end()) {
      surfaces.push_back(
          std::make_unique<SurfaceBlur>(surface, region, offset));
      return;
    }
    pixman_region32_copy(&(*found)->region, region);
    (*found)->offset = offset;
  }

  void ClearSurfaceBlur(wlr_surface* surface) {
    std::erase_if(surfaces, [surface](const auto& item) {
      return item->surface == surface;
    });
  }

  void SetTextureBlur(const void* owner, wlr_texture* texture,
                      const pixman_region32_t* region, float offset) {
    auto found = std::find_if(
        textures_blur.begin(), textures_blur.end(),
        [owner](const auto& item) { return item->owner == owner; });
    if (found == textures_blur.end()) {
      textures_blur.push_back(
          std::make_unique<TextureBlur>(owner, texture, region, offset));
      return;
    }
    (*found)->texture = texture;
    pixman_region32_copy(&(*found)->region, region);
    (*found)->offset = offset;
  }

  void ClearTextureBlur(const void* owner) {
    std::erase_if(textures_blur,
                  [owner](const auto& item) { return item->owner == owner; });
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
    self->renderer->ApplySurfaceBlur(self, options);
    if (!self->failed && self->inner != nullptr) {
      wlr_render_pass_add_texture(self->inner, options);
    }
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

  bool EnsureGlResources(int width, int height) {
    if (program == 0) {
      program = CreateProgram();
      if (program == 0) {
        return false;
      }
      image_uniform = glGetUniformLocation(program, "image");
      direction_uniform = glGetUniformLocation(program, "direction");
      position_attribute = glGetAttribLocation(program, "pos");
    }

    if (target_width == width && target_height == height && textures[0] != 0) {
      return true;
    }

    glDeleteFramebuffers(2, framebuffers);
    glDeleteTextures(2, textures);
    framebuffers[0] = framebuffers[1] = 0;
    textures[0] = textures[1] = 0;
    target_width = target_height = 0;

    glGenTextures(2, textures);
    glGenFramebuffers(2, framebuffers);
    for (size_t i = 0; i < 2; ++i) {
      glBindTexture(GL_TEXTURE_2D, textures[i]);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
                   GL_UNSIGNED_BYTE, nullptr);
      glBindFramebuffer(GL_FRAMEBUFFER, framebuffers[i]);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_TEXTURE_2D, textures[i], 0);
      if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        ABSL_LOG(ERROR) << "Failed to allocate backdrop blur framebuffer";
        glDeleteFramebuffers(2, framebuffers);
        glDeleteTextures(2, textures);
        framebuffers[0] = framebuffers[1] = 0;
        textures[0] = textures[1] = 0;
        return false;
      }
    }
    target_width = width;
    target_height = height;
    return true;
  }

  void DrawTexture(GLuint texture, GLuint framebuffer, float direction_x,
                   float direction_y, const pixman_region32_t* clip) const {
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glViewport(0, 0, target_width, target_height);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glUseProgram(program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(image_uniform, 0);
    glUniform2f(direction_uniform, direction_x, direction_y);
    glEnableVertexAttribArray(position_attribute);

    pixman_region32_t full;
    if (clip == nullptr) {
      pixman_region32_init_rect(&full, 0, 0, target_width, target_height);
      clip = &full;
    }
    int rectangle_count = 0;
    const pixman_box32_t* rectangles =
        pixman_region32_rectangles(clip, &rectangle_count);
    std::vector<GLfloat> vertices;
    vertices.reserve(static_cast<size_t>(rectangle_count) * 12);
    for (int i = 0; i < rectangle_count; ++i) {
      const float x1 = static_cast<float>(rectangles[i].x1) / target_width;
      const float y1 = static_cast<float>(rectangles[i].y1) / target_height;
      const float x2 = static_cast<float>(rectangles[i].x2) / target_width;
      const float y2 = static_cast<float>(rectangles[i].y2) / target_height;
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

  void ApplyRegionBlur(RenderPass* pass, pixman_region32_t* clip,
                       float offset) {
    if (pixman_region32_empty(clip)) return;

    if (vulkan_blur != nullptr) {
      const bool submitted = wlr_render_pass_submit(pass->inner);
      pass->inner = nullptr;
      if (!submitted) {
        pass->failed = true;
        return;
      }

      wlr_texture* blurred = vulkan_blur->Render(pass->target, offset);
      pass->inner = wlr_renderer_begin_buffer_pass(inner, pass->target,
                                                   &pass->intermediate_options);
      if (pass->inner == nullptr) {
        pass->failed = true;
        return;
      }
      if (blurred != nullptr) {
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

    GLint output_framebuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &output_framebuffer);
    if (!EnsureGlResources(pass->target->width, pass->target->height)) {
      glBindFramebuffer(GL_FRAMEBUFFER, output_framebuffer);
      return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, output_framebuffer);
    glBindTexture(GL_TEXTURE_2D, textures[0]);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, target_width,
                        target_height);
    const float safe_offset = std::clamp(offset, 0.001F, 64.0F);
    const float horizontal = safe_offset / target_width;
    const float vertical = safe_offset / target_height;
    constexpr uint32_t kIterations = 3;
    for (uint32_t i = 0; i < kIterations; ++i) {
      DrawTexture(textures[0], framebuffers[1], horizontal, 0, nullptr);
      DrawTexture(textures[1], framebuffers[0], 0, vertical, nullptr);
    }
    DrawTexture(textures[0], static_cast<GLuint>(output_framebuffer), 0, 0,
                clip);
    glBindFramebuffer(GL_FRAMEBUFFER, output_framebuffer);
    glViewport(0, 0, target_width, target_height);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
  }

  void ApplySurfaceBlur(RenderPass* pass,
                        const wlr_render_texture_options* options) {
    if (pass->target->width <= 0 || pass->target->height <= 0) return;
    auto found = std::find_if(
        surfaces.begin(), surfaces.end(), [options, pass](const auto& item) {
          return !pass->rendered_blurs.contains(item->surface) &&
                 wlr_surface_get_texture(item->surface) == options->texture;
        });
    if (found != surfaces.end()) {
      SurfaceBlur& blur = **found;
      pass->rendered_blurs.insert(blur.surface);
      pixman_region32_t clip = SurfaceRegion(
          blur, *options, pass->target->width, pass->target->height);
      ApplyRegionBlur(pass, &clip, blur.offset);
      pixman_region32_fini(&clip);
      return;
    }

    auto texture = std::find_if(
        textures_blur.begin(), textures_blur.end(),
        [options, pass](const auto& item) {
          return !pass->rendered_texture_blurs.contains(item->owner) &&
                 item->texture == options->texture;
        });
    if (texture == textures_blur.end()) return;
    TextureBlur& blur = **texture;
    pass->rendered_texture_blurs.insert(blur.owner);
    pixman_region32_t clip = TextureRegion(blur, *options, pass->target->width,
                                           pass->target->height);
    ApplyRegionBlur(pass, &clip, blur.offset);
    pixman_region32_fini(&clip);
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
      glDeleteFramebuffers(2, framebuffers);
      glDeleteTextures(2, textures);
      if (previous_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(previous_display, previous_draw, previous_read,
                       previous_context);
      } else {
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
      }
    }
    program = 0;
    framebuffers[0] = framebuffers[1] = 0;
    textures[0] = textures[1] = 0;
  }

  wlr_renderer base = {};
  wlr_renderer* inner = nullptr;
  std::unique_ptr<VulkanBlurPipeline> vulkan_blur;
  std::vector<std::unique_ptr<SurfaceBlur>> surfaces;
  std::vector<std::unique_ptr<TextureBlur>> textures_blur;
  GLuint program = 0;
  GLint image_uniform = -1;
  GLint direction_uniform = -1;
  GLint position_attribute = -1;
  GLuint textures[2] = {};
  GLuint framebuffers[2] = {};
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

void BackdropBlurRenderer::SetAllocator(wlr_allocator* allocator) {
  impl_->SetAllocator(allocator);
}

void BackdropBlurRenderer::SetSurfaceBlur(wlr_surface* surface,
                                          const pixman_region32_t* region,
                                          float offset) {
  if (surface != nullptr && region != nullptr) {
    impl_->SetSurfaceBlur(surface, region, offset);
  }
}

void BackdropBlurRenderer::ClearSurfaceBlur(wlr_surface* surface) {
  impl_->ClearSurfaceBlur(surface);
}

void BackdropBlurRenderer::SetTextureBlur(const void* owner,
                                          wlr_texture* texture,
                                          const pixman_region32_t* region,
                                          float offset) {
  if (owner != nullptr && texture != nullptr && region != nullptr) {
    impl_->SetTextureBlur(owner, texture, region, offset);
  }
}

void BackdropBlurRenderer::ClearTextureBlur(const void* owner) {
  impl_->ClearTextureBlur(owner);
}

}  // namespace render
}  // namespace flakewm
