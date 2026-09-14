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
 * This is an implementation of GXDE Screenshot protocol.
 */

#include <drm_fourcc.h>

extern "C" {
#include <wlr/interfaces/wlr_ext_image_capture_source_v1.h>
#include <wlr/util/transform.h>

wlr_allocator* wlr_shm_allocator_create(void);
}

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "protocol/gxde-screenshot-v1-protocol.h"
#include "src/protocol/gxde/gxde_protocol_manager_internal.h"
#include "src/utils/signal_listener.h"

namespace flakewm {
namespace protocol {
namespace {

constexpr uint32_t kProtocolVersion = 1;
constexpr int kMaximumDimension = 16384;
constexpr uint64_t kMaximumBytes = 256ULL * 1024ULL * 1024ULL;

struct SceneSourceFrameEvent {
  wlr_ext_image_capture_source_v1_frame_event base;
  wlr_buffer* buffer;
  timespec when;
};

bool ValidSize(int width, int height) {
  return width > 0 && height > 0 && width <= kMaximumDimension &&
         height <= kMaximumDimension &&
         static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4 <=
             kMaximumBytes;
}

bool IsFourByteFormat(uint32_t format) {
  switch (format) {
    case DRM_FORMAT_ARGB8888:
    case DRM_FORMAT_XRGB8888:
    case DRM_FORMAT_ABGR8888:
    case DRM_FORMAT_XBGR8888:
    case DRM_FORMAT_RGBA8888:
    case DRM_FORMAT_RGBX8888:
    case DRM_FORMAT_BGRA8888:
    case DRM_FORMAT_BGRX8888:
      return true;
    default:
      return false;
  }
}

}  // namespace

struct GxdeScreenshotManager::Binding {
  GxdeScreenshotManager* manager;
  wl_resource* resource;
};

class GxdeScreenshotManager::Frame final {
 public:
  Frame(GxdeScreenshotManager* manager, Binding* binding, wl_resource* resource)
      : manager_(manager), binding_(binding), resource_(resource) {}

  ~Frame() {
    StopCapture(true);
    ReleaseBuffer();
    if (resource_ != nullptr) {
      wl_resource_set_user_data(resource_, nullptr);
    }
    if (manager_ != nullptr) {
      manager_->RemoveFrame(this);
    }
  }

  Frame(const Frame&) = delete;
  Frame& operator=(const Frame&) = delete;

  void Install() {
    static const struct gxde_screenshot_frame_v1_interface implementation = {
        .destroy = DestroyRequest,
        .release_buffer = ReleaseBufferRequest,
    };
    wl_resource_set_implementation(resource_, &implementation, this,
                                   DestroyResource);
  }

  void BeginOutput(wlr_output* output, const wlr_box& box,
                   bool overlay_cursor) {
    if (!CanCapture() || output == nullptr ||
        !ValidSize(box.width, box.height)) {
      Fail();
      return;
    }
    output_ = output;
    source_box_ = box;
    overlay_cursor_ = overlay_cursor;
    output_destroy_.Connect(&output_->events.destroy);
    output_commit_.Connect(&output_->events.commit);
    wlr_output_lock_attach_render(output_, true);
    if (overlay_cursor_) {
      wlr_output_lock_software_cursors(output_, true);
    }
    capture_started_ = true;
    wlr_output_schedule_frame(output_);
  }

  // Width and height are adjacent by design: they mirror the protocol request.
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  void BeginScene(wlr_ext_image_capture_source_v1* source, uint32_t max_width,
                  uint32_t max_height) {
    if (!CanCapture() || source == nullptr || source->impl == nullptr ||
        source->impl->start == nullptr) {
      Fail();
      return;
    }
    source_ = source;
    max_width_ = max_width;
    max_height_ = max_height;
    source_destroy_.Connect(&source_->events.destroy);
    source_frame_.Connect(&source_->events.frame);
    wl_event_loop* event_loop = manager_->owner_->EventLoop();
    if (event_loop == nullptr) {
      Fail();
      return;
    }
    start_idle_ = wl_event_loop_add_idle(event_loop, StartScene, this);
    if (start_idle_ == nullptr) {
      Fail();
    }
  }

  // Width and height are adjacent by design: they mirror the protocol request.
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  void BeginSurface(wlr_surface* surface, uint32_t max_width,
                    uint32_t max_height) {
    if (!CanCapture() || surface == nullptr || !surface->mapped) {
      Fail();
      return;
    }
    surface_ = surface;
    max_width_ = max_width;
    max_height_ = max_height;
    surface_destroy_.Connect(&surface_->events.destroy);
    surface_commit_.Connect(&surface_->events.commit);
    capture_started_ = true;
    CaptureSurface();
  }

  Binding* OwnerBinding() const { return binding_; }
  wl_resource* Resource() const { return resource_; }

 private:
  static void DestroyRequest(wl_client*, wl_resource* resource) {
    wl_resource_destroy(resource);
  }

  static void ReleaseBufferRequest(wl_client*, wl_resource* resource,
                                   uint32_t want_buffer) {
    auto* frame = static_cast<Frame*>(wl_resource_get_user_data(resource));
    if (frame != nullptr) {
      frame->Release(want_buffer != 0);
    }
  }

  static void DestroyResource(wl_resource* resource) {
    auto* frame = static_cast<Frame*>(wl_resource_get_user_data(resource));
    if (frame == nullptr) {
      return;
    }
    frame->resource_ = nullptr;
    delete frame;
  }

  static void StartScene(void* data) {
    auto* frame = static_cast<Frame*>(data);
    frame->start_idle_ = nullptr;
    if (!frame->CanCapture() || frame->source_ == nullptr ||
        frame->source_->impl == nullptr ||
        frame->source_->impl->start == nullptr) {
      frame->Fail();
      return;
    }
    frame->capture_started_ = true;
    frame->source_->impl->start(frame->source_, false);
  }

  static void OnOutputCommit(Frame* frame, wlr_output_event_commit* event) {
    if (frame->buffer_ != nullptr || event == nullptr ||
        event->state == nullptr ||
        (event->state->committed & WLR_OUTPUT_STATE_BUFFER) == 0 ||
        event->state->buffer == nullptr) {
      return;
    }
    wlr_buffer* source = event->state->buffer;
    const wlr_box& box = frame->source_box_;
    if (box.x < 0 || box.y < 0 || box.width <= 0 || box.height <= 0 ||
        static_cast<int64_t>(box.x) + box.width > source->width ||
        static_cast<int64_t>(box.y) + box.height > source->height) {
      frame->Fail();
      return;
    }
    wlr_texture* texture =
        wlr_texture_from_buffer(frame->manager_->owner_->Renderer(), source);
    if (texture == nullptr || !frame->CopyTexture(texture, box)) {
      if (texture != nullptr) {
        wlr_texture_destroy(texture);
      }
      frame->Fail();
      return;
    }
    wlr_texture_destroy(texture);
  }

  static void OnOutputDestroy(Frame* frame, void*) {
    frame->capture_started_ = false;
    frame->output_commit_.Disconnect();
    frame->output_destroy_.Disconnect();
    frame->output_ = nullptr;
    frame->Cancel();
  }

  static void OnSourceFrame(
      Frame* frame, wlr_ext_image_capture_source_v1_frame_event* base_event) {
    if (frame->buffer_ != nullptr || base_event == nullptr) {
      return;
    }
    auto* event = reinterpret_cast<SceneSourceFrameEvent*>(base_event);
    if (event->buffer == nullptr) {
      frame->Fail();
      return;
    }
    wlr_texture* texture = wlr_texture_from_buffer(
        frame->manager_->owner_->Renderer(), event->buffer);
    const wlr_box box = {
        .x = 0,
        .y = 0,
        .width = event->buffer->width,
        .height = event->buffer->height,
    };
    if (texture == nullptr || !frame->CopyTexture(texture, box)) {
      if (texture != nullptr) {
        wlr_texture_destroy(texture);
      }
      frame->Fail();
      return;
    }
    wlr_texture_destroy(texture);
  }

  static void OnSourceDestroy(Frame* frame, void*) {
    frame->capture_started_ = false;
    frame->source_frame_.Disconnect();
    frame->source_destroy_.Disconnect();
    frame->source_ = nullptr;
    frame->Cancel();
  }

  static void OnSurfaceCommit(Frame* frame, void*) {
    if (frame->buffer_ == nullptr) {
      frame->CaptureSurface();
    }
  }

  static void OnSurfaceDestroy(Frame* frame, void*) {
    frame->capture_started_ = false;
    frame->surface_commit_.Disconnect();
    frame->surface_destroy_.Disconnect();
    frame->surface_ = nullptr;
    frame->Cancel();
  }

  bool CanCapture() const {
    return manager_ != nullptr && manager_->owner_ != nullptr &&
           manager_->owner_->CaptureAllowed() && resource_ != nullptr &&
           !terminal_;
  }

  void CaptureSurface() {
    if (!CanCapture() || surface_ == nullptr || !surface_->mapped ||
        buffer_ != nullptr) {
      return;
    }
    wlr_texture* texture = wlr_surface_get_texture(surface_);
    if (texture == nullptr) {
      Fail();
      return;
    }
    const wlr_box box = {
        .x = 0,
        .y = 0,
        .width = static_cast<int>(texture->width),
        .height = static_cast<int>(texture->height),
    };
    if (!CopyTexture(texture, box)) {
      Fail();
    }
  }

  bool CopyTexture(wlr_texture* texture, const wlr_box& box) {
    if (!CanCapture() || texture == nullptr ||
        !ValidSize(box.width, box.height)) {
      return false;
    }

    int target_width = box.width;
    int target_height = box.height;
    double scale = 1.0;
    if (max_width_ > 0 && max_width_ < static_cast<uint32_t>(target_width)) {
      scale = std::min(scale, static_cast<double>(max_width_) / target_width);
    }
    if (max_height_ > 0 && max_height_ < static_cast<uint32_t>(target_height)) {
      scale = std::min(scale, static_cast<double>(max_height_) / target_height);
    }
    target_width =
        std::max(1, static_cast<int>(std::floor(target_width * scale)));
    target_height =
        std::max(1, static_cast<int>(std::floor(target_height * scale)));
    if (!ValidSize(target_width, target_height)) {
      return false;
    }

    const uint32_t preferred = wlr_texture_preferred_read_format(texture);
    const uint32_t format =
        IsFourByteFormat(preferred) ? preferred : DRM_FORMAT_ARGB8888;

    wlr_buffer* readback = AllocateBuffer(box.width, box.height, format);
    if (readback == nullptr) {
      return false;
    }
    void* source_data = nullptr;
    uint32_t source_format = 0;
    size_t source_stride = 0;
    if (!wlr_buffer_begin_data_ptr_access(
            readback, WLR_BUFFER_DATA_PTR_ACCESS_WRITE, &source_data,
            &source_format, &source_stride)) {
      wlr_buffer_drop(readback);
      return false;
    }
    const wlr_texture_read_pixels_options options = {
        .data = source_data,
        .format = source_format,
        .stride = static_cast<uint32_t>(source_stride),
        .src_box = box,
    };
    const bool read = wlr_texture_read_pixels(texture, &options);
    wlr_buffer_end_data_ptr_access(readback);
    if (!read) {
      wlr_buffer_drop(readback);
      return false;
    }

    wlr_buffer* result = readback;
    if (target_width != box.width || target_height != box.height) {
      result = ScaleBuffer(readback, target_width, target_height, format);
      wlr_buffer_drop(readback);
      if (result == nullptr) {
        return false;
      }
    }
    return SendBuffer(result);
  }

  // Buffer allocation APIs conventionally keep width, height, and format
  // together in this order.
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  wlr_buffer* AllocateBuffer(int width, int height, uint32_t format) const {
    if (manager_->shm_allocator_ == nullptr || !ValidSize(width, height)) {
      return nullptr;
    }
    wlr_drm_format drm_format = {
        .format = format,
    };
    return wlr_allocator_create_buffer(manager_->shm_allocator_, width, height,
                                       &drm_format);
  }

  wlr_buffer* ScaleBuffer(wlr_buffer* source, int width, int height,
                          uint32_t format) const {
    wlr_buffer* destination = AllocateBuffer(width, height, format);
    if (destination == nullptr) {
      return nullptr;
    }
    void* source_data = nullptr;
    void* destination_data = nullptr;
    uint32_t source_format = 0;
    uint32_t destination_format = 0;
    size_t source_stride = 0;
    size_t destination_stride = 0;
    if (!wlr_buffer_begin_data_ptr_access(
            source, WLR_BUFFER_DATA_PTR_ACCESS_READ, &source_data,
            &source_format, &source_stride)) {
      wlr_buffer_drop(destination);
      return nullptr;
    }
    if (!wlr_buffer_begin_data_ptr_access(
            destination, WLR_BUFFER_DATA_PTR_ACCESS_WRITE, &destination_data,
            &destination_format, &destination_stride)) {
      wlr_buffer_end_data_ptr_access(source);
      wlr_buffer_drop(destination);
      return nullptr;
    }
    if (source_format != destination_format) {
      wlr_buffer_end_data_ptr_access(destination);
      wlr_buffer_end_data_ptr_access(source);
      wlr_buffer_drop(destination);
      return nullptr;
    }
    const auto* source_bytes = static_cast<const uint8_t*>(source_data);
    auto* destination_bytes = static_cast<uint8_t*>(destination_data);
    for (int y = 0; y < height; ++y) {
      const int source_y =
          static_cast<int>(static_cast<int64_t>(y) * source->height / height);
      for (int x = 0; x < width; ++x) {
        const int source_x =
            static_cast<int>(static_cast<int64_t>(x) * source->width / width);
        std::memcpy(destination_bytes + y * destination_stride +
                        static_cast<size_t>(x) * 4,
                    source_bytes + source_y * source_stride +
                        static_cast<size_t>(source_x) * 4,
                    4);
      }
    }
    wlr_buffer_end_data_ptr_access(destination);
    wlr_buffer_end_data_ptr_access(source);
    return destination;
  }

  bool SendBuffer(wlr_buffer* buffer) {
    wlr_shm_attributes attributes = {};
    if (resource_ == nullptr || buffer == nullptr ||
        !wlr_buffer_get_shm(buffer, &attributes)) {
      if (buffer != nullptr) {
        wlr_buffer_drop(buffer);
      }
      return false;
    }
    ReleaseBuffer();
    buffer_ = buffer;
    gxde_screenshot_frame_v1_send_buffer(
        resource_, attributes.fd, attributes.format,
        static_cast<uint32_t>(buffer_->width),
        static_cast<uint32_t>(buffer_->height), attributes.offset,
        attributes.stride, 0, 0, 0);
    gxde_screenshot_frame_v1_send_buffer_done(resource_);
    return true;
  }

  void Release(bool want_buffer) {
    if (terminal_) {
      ReleaseBuffer();
      return;
    }
    ReleaseBuffer();
    if (!want_buffer || !manager_->owner_->CaptureAllowed()) {
      StopCapture(true);
      return;
    }
    if (output_ != nullptr) {
      wlr_output_schedule_frame(output_);
    } else if (source_ != nullptr && source_->impl != nullptr &&
               source_->impl->request_frame != nullptr) {
      source_->impl->request_frame(source_, true);
    }
  }

  void ReleaseBuffer() {
    if (buffer_ != nullptr) {
      wlr_buffer_drop(buffer_);
      buffer_ = nullptr;
    }
  }

  void StopCapture(bool unlock_output) {
    if (start_idle_ != nullptr) {
      wl_event_source_remove(start_idle_);
      start_idle_ = nullptr;
    }
    output_commit_.Disconnect();
    output_destroy_.Disconnect();
    source_frame_.Disconnect();
    source_destroy_.Disconnect();
    surface_commit_.Disconnect();
    surface_destroy_.Disconnect();
    if (capture_started_ && output_ != nullptr && unlock_output) {
      wlr_output_lock_attach_render(output_, false);
      if (overlay_cursor_) {
        wlr_output_lock_software_cursors(output_, false);
      }
    } else if (capture_started_ && source_ != nullptr &&
               source_->impl != nullptr && source_->impl->stop != nullptr) {
      source_->impl->stop(source_);
    }
    capture_started_ = false;
    output_ = nullptr;
    source_ = nullptr;
    surface_ = nullptr;
  }

  void Fail() {
    if (resource_ == nullptr || terminal_) {
      return;
    }
    terminal_ = true;
    StopCapture(true);
    ReleaseBuffer();
    gxde_screenshot_frame_v1_send_failed(resource_);
  }

  void Cancel() {
    if (resource_ == nullptr || terminal_) {
      return;
    }
    terminal_ = true;
    StopCapture(false);
    ReleaseBuffer();
    gxde_screenshot_frame_v1_send_cancelled(resource_);
  }

  GxdeScreenshotManager* manager_;
  Binding* binding_;
  wl_resource* resource_;
  wlr_output* output_ = nullptr;
  wlr_ext_image_capture_source_v1* source_ = nullptr;
  wlr_surface* surface_ = nullptr;
  wlr_buffer* buffer_ = nullptr;
  wlr_box source_box_ = {};
  uint32_t max_width_ = 0;
  uint32_t max_height_ = 0;
  bool overlay_cursor_ = false;
  bool capture_started_ = false;
  bool terminal_ = false;
  wl_event_source* start_idle_ = nullptr;
  utils::SignalListener<Frame, wlr_output_event_commit> output_commit_{
      this, OnOutputCommit};
  utils::SignalListener<Frame, void> output_destroy_{this, OnOutputDestroy};
  utils::SignalListener<Frame, wlr_ext_image_capture_source_v1_frame_event>
      source_frame_{this, OnSourceFrame};
  utils::SignalListener<Frame, void> source_destroy_{this, OnSourceDestroy};
  utils::SignalListener<Frame, void> surface_commit_{this, OnSurfaceCommit};
  utils::SignalListener<Frame, void> surface_destroy_{this, OnSurfaceDestroy};
};

GxdeScreenshotManager::GxdeScreenshotManager(GxdeProtocolManager* owner,
                                             wl_display* display)
    : owner_(owner), shm_allocator_(wlr_shm_allocator_create()) {
  if (display != nullptr && shm_allocator_ != nullptr) {
    global_ = wl_global_create(display, &gxde_screenshot_manager_v1_interface,
                               kProtocolVersion, this, Bind);
  }
}

GxdeScreenshotManager::~GxdeScreenshotManager() {
  if (global_ != nullptr) {
    wl_global_destroy(global_);
    global_ = nullptr;
  }
  while (!bindings_.empty()) {
    wl_resource_destroy(bindings_.back()->resource);
  }
  while (!frames_.empty()) {
    wl_resource_destroy(frames_.back()->Resource());
  }
  if (shm_allocator_ != nullptr) {
    wlr_allocator_destroy(shm_allocator_);
    shm_allocator_ = nullptr;
  }
}

bool GxdeScreenshotManager::IsValid() const {
  return global_ != nullptr && shm_allocator_ != nullptr;
}

void GxdeScreenshotManager::Bind(wl_client* client, void* data,
                                 uint32_t version, uint32_t id) {
  static const struct gxde_screenshot_manager_v1_interface implementation = {
      .capture_output = CaptureOutput,
      .capture_output_region = CaptureOutputRegion,
      .capture_window = CaptureWindow,
      .capture_window_thumbnail = CaptureWindowThumbnail,
      .destroy = DestroyManager,
  };
  auto* manager = static_cast<GxdeScreenshotManager*>(data);
  wl_resource* resource = wl_resource_create(
      client, &gxde_screenshot_manager_v1_interface,
      static_cast<int>(std::min(version, kProtocolVersion)), id);
  if (resource == nullptr) {
    wl_client_post_no_memory(client);
    return;
  }
  auto* binding = new Binding{.manager = manager, .resource = resource};
  manager->bindings_.push_back(binding);
  wl_resource_set_implementation(resource, &implementation, binding,
                                 DestroyBinding);
}

void GxdeScreenshotManager::DestroyBinding(wl_resource* resource) {
  auto* binding = static_cast<Binding*>(wl_resource_get_user_data(resource));
  if (binding == nullptr) {
    return;
  }
  GxdeScreenshotManager* manager = binding->manager;
  while (true) {
    auto iterator = std::find_if(
        manager->frames_.begin(), manager->frames_.end(),
        [binding](Frame* frame) { return frame->OwnerBinding() == binding; });
    if (iterator == manager->frames_.end()) {
      break;
    }
    wl_resource_destroy((*iterator)->Resource());
  }
  manager->RemoveBinding(binding);
  delete binding;
}

void GxdeScreenshotManager::DestroyManager(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

GxdeScreenshotManager::Frame* GxdeScreenshotManager::CreateFrame(
    wl_client* client, wl_resource* manager_resource, uint32_t id) {
  auto* binding =
      static_cast<Binding*>(wl_resource_get_user_data(manager_resource));
  if (binding == nullptr || binding->manager != this) {
    return nullptr;
  }
  wl_resource* resource =
      wl_resource_create(client, &gxde_screenshot_frame_v1_interface,
                         wl_resource_get_version(manager_resource), id);
  if (resource == nullptr) {
    wl_client_post_no_memory(client);
    return nullptr;
  }
  auto* frame = new Frame(this, binding, resource);
  frames_.push_back(frame);
  frame->Install();
  return frame;
}

void GxdeScreenshotManager::CaptureOutput(wl_client* client,
                                          wl_resource* resource, uint32_t id,
                                          const char* output,
                                          uint32_t overlay_cursor) {
  auto* binding = static_cast<Binding*>(wl_resource_get_user_data(resource));
  if (binding == nullptr) {
    return;
  }
  Frame* frame = binding->manager->CreateFrame(client, resource, id);
  if (frame == nullptr) {
    return;
  }
  wlr_output* handle = binding->manager->owner_->FindOutput(output);
  const wlr_box box = {
      .x = 0,
      .y = 0,
      .width = handle == nullptr ? 0 : handle->width,
      .height = handle == nullptr ? 0 : handle->height,
  };
  frame->BeginOutput(handle, box, overlay_cursor != 0);
}

// Signature is fixed by the generated Wayland server interface.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void GxdeScreenshotManager::CaptureOutputRegion(
    wl_client* client, wl_resource* resource, uint32_t id, const char* output,
    int32_t x, int32_t y, uint32_t width,
    uint32_t height,  // NOLINT(bugprone-easily-swappable-parameters)
    uint32_t overlay_cursor) {
  auto* binding = static_cast<Binding*>(wl_resource_get_user_data(resource));
  if (binding == nullptr) {
    return;
  }
  Frame* frame = binding->manager->CreateFrame(client, resource, id);
  if (frame == nullptr) {
    return;
  }
  wlr_output* handle = binding->manager->owner_->FindOutput(output);
  int effective_width = 0;
  int effective_height = 0;
  if (handle != nullptr) {
    wlr_output_effective_resolution(handle, &effective_width,
                                    &effective_height);
  }
  if (handle == nullptr || x < 0 || y < 0 || width == 0 || height == 0 ||
      width > INT_MAX || height > INT_MAX ||
      static_cast<int64_t>(x) + width > effective_width ||
      static_cast<int64_t>(y) + height > effective_height) {
    frame->BeginOutput(nullptr, {}, false);
    return;
  }
  wlr_box logical = {
      .x = x,
      .y = y,
      .width = static_cast<int>(width),
      .height = static_cast<int>(height),
  };
  wlr_box box = {};
  wlr_box_transform(&box, &logical,
                    wlr_output_transform_invert(handle->transform),
                    effective_width, effective_height);
  box.x = static_cast<int>(static_cast<double>(box.x) * handle->scale);
  box.y = static_cast<int>(static_cast<double>(box.y) * handle->scale);
  box.width = static_cast<int>(static_cast<double>(box.width) * handle->scale);
  box.height =
      static_cast<int>(static_cast<double>(box.height) * handle->scale);
  frame->BeginOutput(handle, box, overlay_cursor != 0);
}

void GxdeScreenshotManager::CaptureWindow(wl_client* client,
                                          wl_resource* resource, uint32_t id,
                                          const char* window,
                                          uint32_t without_decoration) {
  CaptureWindowThumbnail(client, resource, id, "", window, 0, 0,
                         without_decoration);
}

// Signature is fixed by the generated Wayland server interface.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void GxdeScreenshotManager::CaptureWindowThumbnail(
    wl_client* client, wl_resource* resource, uint32_t id, const char* app_id,
    const char* window, uint32_t max_width,
    uint32_t max_height,  // NOLINT(bugprone-easily-swappable-parameters)
    uint32_t without_decoration) {
  auto* binding = static_cast<Binding*>(wl_resource_get_user_data(resource));
  if (binding == nullptr) {
    return;
  }
  Frame* frame = binding->manager->CreateFrame(client, resource, id);
  if (frame == nullptr) {
    return;
  }
  GxdeProtocolManager::WindowTarget target =
      binding->manager->owner_->FindWindow(window, app_id,
                                           without_decoration != 0);
  if (without_decoration != 0) {
    frame->BeginSurface(target.surface, max_width, max_height);
  } else {
    frame->BeginScene(target.scene_source, max_width, max_height);
  }
}

void GxdeScreenshotManager::RemoveFrame(Frame* frame) {
  std::erase(frames_, frame);
}

void GxdeScreenshotManager::RemoveBinding(Binding* binding) {
  std::erase(bindings_, binding);
}

}  // namespace protocol
}  // namespace flakewm
