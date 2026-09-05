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

#include <absl/log/absl_log.h>
#include <drm_fourcc.h>
#include <fcntl.h>
#include <pixman.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <utility>

#include "src/backend/fbdev/fbdev_backend/fbdev_backend.h"
#include "src/backend/fbdev/fbdev_output/fbdev_output.h"

namespace flakewm {
namespace backend {
namespace fbdev {
namespace {

constexpr uint32_t kCommittedState =
    WLR_OUTPUT_STATE_BUFFER | WLR_OUTPUT_STATE_MODE | WLR_OUTPUT_STATE_ENABLED;
constexpr uint32_t kSupportedState =
    WLR_OUTPUT_STATE_BACKEND_OPTIONAL | kCommittedState;

uint32_t CalculatePixelFormat(const fb_var_screeninfo& variable,
                              const fb_fix_screeninfo& fixed) {
  if (fixed.type != FB_TYPE_PACKED_PIXELS ||
      (fixed.visual != FB_VISUAL_TRUECOLOR &&
       fixed.visual != FB_VISUAL_DIRECTCOLOR) ||
      variable.grayscale != 0 || variable.red.msb_right != 0 ||
      variable.green.msb_right != 0 || variable.blue.msb_right != 0) {
    return DRM_FORMAT_INVALID;
  }

  if (variable.bits_per_pixel == 16 && variable.red.offset == 11 &&
      variable.green.offset == 5 && variable.blue.offset == 0) {
    return DRM_FORMAT_RGB565;
  }
  if (variable.bits_per_pixel == 24 && variable.red.offset == 16 &&
      variable.green.offset == 8 && variable.blue.offset == 0) {
    return DRM_FORMAT_RGB888;
  }
  if (variable.bits_per_pixel != 32) {
    return DRM_FORMAT_INVALID;
  }

  if (variable.red.offset == 16 && variable.green.offset == 8 &&
      variable.blue.offset == 0) {
    return variable.transp.length == 0 ? DRM_FORMAT_XRGB8888
                                       : DRM_FORMAT_ARGB8888;
  }
  if (variable.red.offset == 24 && variable.green.offset == 16 &&
      variable.blue.offset == 8 && variable.transp.offset == 0) {
    return variable.transp.length == 0 ? DRM_FORMAT_RGBX8888
                                       : DRM_FORMAT_RGBA8888;
  }
  if (variable.blue.offset == 16 && variable.green.offset == 8 &&
      variable.red.offset == 0) {
    return variable.transp.length == 0 ? DRM_FORMAT_XBGR8888
                                       : DRM_FORMAT_ABGR8888;
  }
  return DRM_FORMAT_INVALID;
}

int32_t CalculateRefresh(const fb_var_screeninfo& info) {
  uint64_t period = info.upper_margin + info.lower_margin + info.yres;
  period *= info.left_margin + info.right_margin + info.xres;
  period *= info.pixclock;
  if (period == 0) {
    return 60000;
  }

  const uint64_t refresh = 1000000000000000ULL / period;
  if (refresh < 1000 || refresh > 1000000 ||
      refresh > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
    return 60000;
  }
  return static_cast<int32_t>(refresh);
}

bool ModesEqual(const fb_var_screeninfo& current,
                const fb_var_screeninfo& requested) {
  return current.xres_virtual >= requested.xres_virtual &&
         current.yres_virtual >= requested.yres_virtual &&
         current.bits_per_pixel == requested.bits_per_pixel &&
         current.red.length == requested.red.length &&
         current.green.length == requested.green.length &&
         current.blue.length == requested.blue.length &&
         current.xres == requested.xres && current.yres == requested.yres &&
         current.right_margin == requested.right_margin &&
         current.hsync_len == requested.hsync_len &&
         current.left_margin == requested.left_margin &&
         current.lower_margin == requested.lower_margin &&
         current.vsync_len == requested.vsync_len &&
         current.upper_margin == requested.upper_margin &&
         current.sync == requested.sync && current.vmode == requested.vmode;
}

bool PendingEnabled(const wlr_output* output, const wlr_output_state* state) {
  if ((state->committed & WLR_OUTPUT_STATE_ENABLED) != 0) {
    return state->enabled;
  }
  return output->enabled;
}

}  // namespace

FbdevOutput::FbdevOutput(FbdevBackend* backend, std::string device, int index) {
  backend_ = backend;
  device_ = std::move(device);
  index_ = index;
}

FbdevOutput::~FbdevOutput() {
  if (frame_timer_ != nullptr) {
    wl_event_source_remove(frame_timer_);
    frame_timer_ = nullptr;
  }
  Unmap();
  Close();
}

FbdevOutput* FbdevOutput::Create(FbdevBackend* backend,
                                 const std::string& device, int index) {
  auto* output = new FbdevOutput(backend, device, index);
  if (!output->Initialize()) {
    if (output->output_initialized_) {
      wlr_output_destroy(&output->output_);
    } else {
      delete output;
    }
    return nullptr;
  }
  return output;
}

bool FbdevOutput::Initialize() {
  if (!Open(&screen_)) {
    return false;
  }

  wlr_output_state state;
  wlr_output_state_init(&state);
  wlr_output_state_set_render_format(&state, screen_.pixel_format);
  wlr_output_state_set_subpixel(&state, WL_OUTPUT_SUBPIXEL_UNKNOWN);
  wlr_output_init(&output_, backend_->Handle(), Implementation(),
                  backend_->EventLoop(), &state);
  wlr_output_state_finish(&state);
  output_initialized_ = true;
  output_.data = this;

  char name[32];
  std::snprintf(name, sizeof(name), "FB-%d", index_);
  wlr_output_set_name(&output_, name);
  wlr_output_set_description(&output_, screen_.description);
  output_.phys_width = static_cast<int32_t>(screen_.width_mm);
  output_.phys_height = static_cast<int32_t>(screen_.height_mm);
  wlr_output_lock_software_cursors(&output_, true);

  mode_.handle.width = static_cast<int32_t>(screen_.width);
  mode_.handle.height = static_cast<int32_t>(screen_.height);
  mode_.handle.refresh = screen_.refresh;
  mode_.handle.preferred = true;
  mode_.screen = screen_.current;
  wl_list_insert(&output_.modes, &mode_.handle.link);
  UpdateFrameDelay(mode_.handle.refresh);

  ABSL_LOG(INFO) << "Found fbdev output " << name << " on " << device_ << ": "
                 << mode_.handle.width << 'x' << mode_.handle.height << " at "
                 << mode_.handle.refresh / 1000.0 << " Hz";
  return true;
}

wlr_output* FbdevOutput::Handle() { return &output_; }

FbdevOutput* FbdevOutput::FromHandle(wlr_output* output) {
  return static_cast<FbdevOutput*>(output->data);
}

const wlr_output_impl* FbdevOutput::Implementation() {
  static const wlr_output_impl implementation = {
      .destroy = Destroy,
      .test = Test,
      .commit = Commit,
  };
  return &implementation;
}

bool FbdevOutput::IsHandle(const wlr_output* output) {
  return output != nullptr && output->impl == Implementation();
}

bool FbdevOutput::Open(ScreenInfo* info) {
  Close();
  fd_ = open(device_.c_str(), O_RDWR | O_CLOEXEC);
  if (fd_ < 0) {
    ABSL_LOG(ERROR) << "Failed to open " << device_ << ": "
                    << std::strerror(errno);
    return false;
  }
  if (!QueryScreenInfo(info)) {
    ABSL_LOG(ERROR) << "Failed to query " << device_;
    Close();
    return false;
  }
  if (!WakeScreen()) {
    ABSL_LOG(WARNING) << "Failed to wake " << device_ << ", continuing anyway";
  }
  return true;
}

void FbdevOutput::Close() {
  if (fd_ >= 0) {
    close(fd_);
    fd_ = -1;
  }
}

bool FbdevOutput::QueryScreenInfo(ScreenInfo* info) const {
  fb_fix_screeninfo fixed = {};
  fb_var_screeninfo variable = {};
  if (ioctl(fd_, FBIOGET_FSCREENINFO, &fixed) < 0 ||
      ioctl(fd_, FBIOGET_VSCREENINFO, &variable) < 0) {
    return false;
  }

  info->current = variable;
  info->width = variable.xres;
  info->height = variable.yres;
  info->width_mm = variable.width == UINT32_MAX ? 0 : variable.width;
  info->height_mm = variable.height == UINT32_MAX ? 0 : variable.height;
  info->bits_per_pixel = variable.bits_per_pixel;
  info->buffer_length = fixed.smem_len;
  info->line_length = fixed.line_length;
  std::snprintf(info->description, sizeof(info->description), "%s", fixed.id);
  info->refresh = CalculateRefresh(variable);
  info->pixel_format = CalculatePixelFormat(variable, fixed);
  if (info->pixel_format == DRM_FORMAT_INVALID) {
    ABSL_LOG(ERROR) << device_ << " uses an unsupported pixel format";
    return false;
  }
  return true;
}

bool FbdevOutput::SetScreenInfo(fb_var_screeninfo* info, bool test_only) const {
  info->activate = test_only ? FB_ACTIVATE_TEST : FB_ACTIVATE_FORCE;
  if (ioctl(fd_, FBIOPUT_VSCREENINFO, info) < 0) {
    ABSL_LOG(ERROR) << "Failed to set mode on " << device_ << ": "
                    << std::strerror(errno);
    return false;
  }
  return true;
}

bool FbdevOutput::WakeScreen() const {
  fb_var_screeninfo info = {};
  if (ioctl(fd_, FBIOGET_VSCREENINFO, &info) < 0) {
    return false;
  }
  info.activate = FB_ACTIVATE_NOW | FB_ACTIVATE_FORCE;
  return ioctl(fd_, FBIOPUT_VSCREENINFO, &info) >= 0;
}

bool FbdevOutput::SetDpms(bool enabled) {
  if (!dpms_supported_ || fd_ < 0) {
    return false;
  }

  const int blank = enabled ? FB_BLANK_UNBLANK : FB_BLANK_POWERDOWN;
  while (ioctl(fd_, FBIOBLANK, blank) < 0) {
    if (errno == EINTR || errno == ERESTART) {
      continue;
    }
    if (errno != EAGAIN) {
      dpms_supported_ = false;
    }
    return false;
  }
  return true;
}

bool FbdevOutput::Map() {
  framebuffer_ =
      mmap(nullptr, screen_.buffer_length, PROT_WRITE, MAP_SHARED, fd_, 0);
  if (framebuffer_ == MAP_FAILED) {
    framebuffer_ = nullptr;
    ABSL_LOG(ERROR) << "Failed to map " << device_ << ": "
                    << std::strerror(errno);
    return false;
  }
  return true;
}

void FbdevOutput::Unmap() {
  if (framebuffer_ == nullptr) {
    return;
  }
  if (munmap(framebuffer_, screen_.buffer_length) < 0) {
    ABSL_LOG(ERROR) << "Failed to unmap " << device_ << ": "
                    << std::strerror(errno);
  }
  framebuffer_ = nullptr;
}

bool FbdevOutput::Enable() {
  if (output_.enabled) {
    return true;
  }
  SetDpms(true);
  if (!Map()) {
    return false;
  }
  frame_timer_ =
      wl_event_loop_add_timer(backend_->EventLoop(), SendFrame, this);
  if (frame_timer_ == nullptr) {
    Unmap();
    return false;
  }
  output_.enabled = true;
  return true;
}

bool FbdevOutput::Disable(bool clear_screen) {
  if (!output_.enabled) {
    return true;
  }
  if (clear_screen && !SetDpms(false) && framebuffer_ != nullptr) {
    std::memset(framebuffer_, 0, screen_.buffer_length);
  }
  if (frame_timer_ != nullptr) {
    wl_event_source_remove(frame_timer_);
    frame_timer_ = nullptr;
  }
  Unmap();
  output_.enabled = false;
  return true;
}

void FbdevOutput::Offscreen() {
  resume_enabled_ = output_.enabled;
  Disable(false);
  Close();
}

bool FbdevOutput::Reenable() {
  ScreenInfo current;
  if (!Open(&current)) {
    return false;
  }

  dpms_supported_ = true;
  if (!ModesEqual(current.current, screen_.current)) {
    fb_var_screeninfo requested = screen_.current;
    if (!SetScreenInfo(&requested, false)) {
      ABSL_LOG(WARNING) << "Failed to restore the mode on " << device_;
    }
  }
  if (!QueryScreenInfo(&screen_)) {
    Close();
    return false;
  }

  if (resume_enabled_ && !Enable()) {
    Close();
    return false;
  }
  if (resume_enabled_) {
    wlr_output_send_frame(&output_);
  }
  return true;
}

bool FbdevOutput::CopyBuffer(const wlr_output_state* state) {
  if (framebuffer_ == nullptr || state->buffer == nullptr) {
    return false;
  }

  void* source = nullptr;
  uint32_t format = DRM_FORMAT_INVALID;
  std::size_t source_stride = 0;
  if (!wlr_buffer_begin_data_ptr_access(state->buffer,
                                        WLR_BUFFER_DATA_PTR_ACCESS_READ,
                                        &source, &format, &source_stride)) {
    return false;
  }

  bool copied = false;
  const std::size_t bytes_per_pixel = screen_.bits_per_pixel / 8;
  const uint32_t copy_width =
      std::min(screen_.width, static_cast<uint32_t>(state->buffer->width));
  const uint32_t copy_height =
      std::min(screen_.height, static_cast<uint32_t>(state->buffer->height));
  const std::size_t x_offset = screen_.current.xoffset * bytes_per_pixel;
  const std::size_t y_offset = screen_.current.yoffset;

  if (format == screen_.pixel_format && bytes_per_pixel != 0 &&
      copy_width * bytes_per_pixel <= source_stride &&
      x_offset + copy_width * bytes_per_pixel <= screen_.line_length &&
      (y_offset + copy_height) * screen_.line_length <= screen_.buffer_length) {
    pixman_region32_t damage;
    if ((state->committed & WLR_OUTPUT_STATE_DAMAGE) != 0) {
      pixman_region32_init(&damage);
      pixman_region32_copy(&damage, &state->damage);
      pixman_region32_intersect_rect(&damage, &damage, 0, 0, copy_width,
                                     copy_height);
    } else {
      pixman_region32_init_rect(&damage, 0, 0, copy_width, copy_height);
    }

    int count = 0;
    const pixman_box32_t* rectangles =
        pixman_region32_rectangles(&damage, &count);
    for (int index = 0; index < count; ++index) {
      for (int32_t y = rectangles[index].y1; y < rectangles[index].y2; ++y) {
        const std::size_t source_offset =
            static_cast<std::size_t>(y) * source_stride +
            static_cast<std::size_t>(rectangles[index].x1) * bytes_per_pixel;
        const std::size_t destination_offset =
            (static_cast<std::size_t>(y) + y_offset) * screen_.line_length +
            x_offset +
            static_cast<std::size_t>(rectangles[index].x1) * bytes_per_pixel;
        const std::size_t size =
            static_cast<std::size_t>(rectangles[index].x2 -
                                     rectangles[index].x1) *
            bytes_per_pixel;
        std::memcpy(static_cast<uint8_t*>(framebuffer_) + destination_offset,
                    static_cast<uint8_t*>(source) + source_offset, size);
      }
    }
    pixman_region32_fini(&damage);
    copied = true;
  }

  wlr_buffer_end_data_ptr_access(state->buffer);
  if (!copied) {
    ABSL_LOG(ERROR) << "Framebuffer layout does not match the render buffer on "
                    << device_;
  }
  return copied;
}

void FbdevOutput::UpdateFrameDelay(int32_t refresh) {
  if (refresh <= 0) {
    refresh = 60000;
  }
  frame_delay_ms_ = std::max(1, 1000000 / refresh);
}

bool FbdevOutput::Test(wlr_output* output_handle,
                       const wlr_output_state* state) {
  FbdevOutput* output = FromHandle(output_handle);
  if (!output->backend_->Session()->active) {
    return false;
  }

  const uint32_t unsupported = state->committed & ~kSupportedState;
  if (unsupported != 0) {
    ABSL_LOG(ERROR) << "Unsupported fbdev output state: 0x" << std::hex
                    << unsupported;
    return false;
  }

  if ((state->committed & WLR_OUTPUT_STATE_MODE) != 0 &&
      (state->mode_type != WLR_OUTPUT_STATE_MODE_FIXED ||
       state->mode != &output->mode_.handle)) {
    return false;
  }

  if ((state->committed & WLR_OUTPUT_STATE_LAYERS) != 0) {
    for (std::size_t index = 0; index < state->layers_len; ++index) {
      state->layers[index].accepted = true;
    }
  }
  return true;
}

bool FbdevOutput::Commit(wlr_output* output_handle,
                         const wlr_output_state* state) {
  FbdevOutput* output = FromHandle(output_handle);
  if (!Test(output_handle, state)) {
    return false;
  }

  if ((state->committed & WLR_OUTPUT_STATE_MODE) != 0) {
    fb_var_screeninfo requested = output->mode_.screen;
    if (!ModesEqual(output->screen_.current, requested) &&
        !output->SetScreenInfo(&requested, false)) {
      return false;
    }
    if (!output->QueryScreenInfo(&output->screen_)) {
      return false;
    }
    output->UpdateFrameDelay(state->mode->refresh);
  }

  if ((state->committed & WLR_OUTPUT_STATE_ENABLED) != 0) {
    if (state->enabled && !output_handle->enabled) {
      if (!output->Enable()) {
        return false;
      }
    } else if (!state->enabled && output_handle->enabled) {
      return output->Disable(true);
    }
  }

  if ((state->committed & WLR_OUTPUT_STATE_BUFFER) != 0 &&
      PendingEnabled(output_handle, state) && !output->CopyBuffer(state)) {
    return false;
  }

  if (PendingEnabled(output_handle, state)) {
    output->pending_present_commit_ = output_handle->commit_seq + 1;
    wl_event_source_timer_update(output->frame_timer_, output->frame_delay_ms_);
  }
  return true;
}

void FbdevOutput::Destroy(wlr_output* output_handle) {
  FbdevOutput* output = FromHandle(output_handle);
  output->backend_->RemoveOutput(output);
  wlr_output_finish(output_handle);
  output->output_initialized_ = false;
  delete output;
}

int FbdevOutput::SendFrame(void* data) {
  auto* output = static_cast<FbdevOutput*>(data);
  if (output->pending_present_commit_ != 0) {
    wlr_output_event_present event = {
        .commit_seq = output->pending_present_commit_,
        .presented = true,
        .refresh =
            output->output_.refresh > 0
                ? static_cast<int>(1000000000000LL / output->output_.refresh)
                : 0,
    };
    wlr_output_send_present(&output->output_, &event);
    output->pending_present_commit_ = 0;
  }
  wlr_output_send_frame(&output->output_);
  return 0;
}

}  // namespace fbdev
}  // namespace backend
}  // namespace flakewm
