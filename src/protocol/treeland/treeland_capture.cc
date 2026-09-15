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
 * Originally copyright by (C) 2024-2026 UnionTech Software Technology Co., Ltd.
 * Original license: Apache-2.0 OR LGPL-3.0-only OR GPL-2.0-only OR
 *                   GPL-3.0-only.
 * Redistributed with GPL-3.0-only.
 * Original code is modified to adapt C++ and Wlroots 0.20.2.
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <memory>
#include <optional>
#include <vector>

extern "C" {
#include <wlr/util/transform.h>
}

#include "protocol/treeland-capture-unstable-v1-protocol.h"
#include "src/protocol/treeland/treeland_protocol_manager_internal.h"
#include "src/utils/signal_listener.h"
#include "src/view/window_selecter/window_selector.h"

namespace flakewm {
namespace protocol {
namespace {

constexpr uint32_t kVersion = 1;

class CaptureGlobal final : public TreelandGlobal {
 public:
  struct Context;

  struct Frame {
    Frame(CaptureGlobal* new_global, Context* new_context,
          wl_resource* new_resource, wlr_output* new_output,
          const wlr_box& new_source_box)
        : global(new_global),
          context(new_context),
          resource(new_resource),
          output(new_output),
          source_box(new_source_box) {}

    CaptureGlobal* global;
    Context* context;
    wl_resource* resource;
    wlr_output* output;
    wlr_box source_box;
    wlr_buffer* source = nullptr;
    bool output_locked = false;
    bool failed = false;
    utils::SignalListener<Frame, wlr_output_event_commit> commit{this,
                                                                 OnCommit};
    utils::SignalListener<Frame, void> destroy{this, OnOutputDestroy};

    static void OnCommit(Frame* frame, wlr_output_event_commit* event) {
      if (frame->source != nullptr || event == nullptr ||
          event->state == nullptr ||
          (event->state->committed & WLR_OUTPUT_STATE_BUFFER) == 0 ||
          event->state->buffer == nullptr) {
        return;
      }
      frame->source = wlr_buffer_lock(event->state->buffer);
      frame->commit.Disconnect();
      frame->UnlockOutput();
      if (frame->source_box.x < 0 || frame->source_box.y < 0 ||
          frame->source_box.width <= 0 || frame->source_box.height <= 0 ||
          static_cast<int64_t>(frame->source_box.x) + frame->source_box.width >
              frame->source->width ||
          static_cast<int64_t>(frame->source_box.y) + frame->source_box.height >
              frame->source->height) {
        frame->Fail();
        return;
      }
      treeland_capture_frame_v1_send_buffer(
          frame->resource, WL_SHM_FORMAT_ARGB8888,
          static_cast<uint32_t>(frame->source_box.width),
          static_cast<uint32_t>(frame->source_box.height),
          static_cast<uint32_t>(frame->source_box.width) * 4);
      treeland_capture_frame_v1_send_buffer_done(frame->resource);
    }
    static void OnOutputDestroy(Frame* frame, void*) {
      frame->commit.Disconnect();
      frame->destroy.Disconnect();
      frame->output = nullptr;
      frame->output_locked = false;
      frame->Fail();
    }
    void UnlockOutput() {
      if (output_locked && output != nullptr) {
        wlr_output_lock_attach_render(output, false);
      }
      output_locked = false;
    }
    void Fail() {
      if (!failed && resource != nullptr) {
        failed = true;
        treeland_capture_frame_v1_send_failed(resource);
      }
    }
  };

  struct Session {
    Session(CaptureGlobal* new_global, Context* new_context,
            wl_resource* new_resource, wlr_output* new_output,
            const wlr_box& new_source_box)
        : global(new_global),
          context(new_context),
          resource(new_resource),
          output(new_output),
          source_box(new_source_box) {}

    CaptureGlobal* global;
    Context* context;
    wl_resource* resource;
    wlr_output* output;
    wlr_box source_box;
    wlr_buffer* buffer = nullptr;
    bool started = false;
    bool awaiting_ack = false;
    bool output_locked = false;
    bool cursor_locked = false;
    uint32_t sec_hi = 0;
    uint32_t sec_lo = 0;
    uint32_t nsec = 0;
    utils::SignalListener<Session, wlr_output_event_commit> commit{this,
                                                                   OnCommit};
    utils::SignalListener<Session, void> destroy{this, OnOutputDestroy};

    static void OnCommit(Session* session, wlr_output_event_commit* event) {
      if (!session->started || session->awaiting_ack || event == nullptr ||
          event->state == nullptr ||
          (event->state->committed & WLR_OUTPUT_STATE_BUFFER) == 0 ||
          event->state->buffer == nullptr) {
        return;
      }
      wlr_dmabuf_attributes attributes = {};
      if (!wlr_buffer_get_dmabuf(event->state->buffer, &attributes) ||
          attributes.n_planes <= 0 ||
          attributes.n_planes > WLR_DMABUF_MAX_PLANES ||
          session->source_box.x < 0 || session->source_box.y < 0 ||
          session->source_box.width <= 0 || session->source_box.height <= 0 ||
          static_cast<int64_t>(session->source_box.x) +
                  session->source_box.width >
              event->state->buffer->width ||
          static_cast<int64_t>(session->source_box.y) +
                  session->source_box.height >
              event->state->buffer->height) {
        treeland_capture_session_v1_send_cancel(
            session->resource,
            TREELAND_CAPTURE_SESSION_V1_CANCEL_REASON_TEMPORARY);
        return;
      }
      if (session->buffer != nullptr) wlr_buffer_unlock(session->buffer);
      session->buffer = wlr_buffer_lock(event->state->buffer);
      const uint32_t modifier_hi = attributes.modifier >> 32;
      const uint32_t modifier_lo = attributes.modifier & 0xffffffffU;
      treeland_capture_session_v1_send_frame(
          session->resource, session->source_box.x, session->source_box.y,
          static_cast<uint32_t>(session->source_box.width),
          static_cast<uint32_t>(session->source_box.height), 0, 0,
          attributes.format, modifier_hi, modifier_lo,
          static_cast<uint32_t>(attributes.n_planes));
      for (int i = 0; i < attributes.n_planes; ++i) {
        const uint64_t size = static_cast<uint64_t>(attributes.stride[i]) *
                              session->buffer->height;
        treeland_capture_session_v1_send_object(
            session->resource, static_cast<uint32_t>(i), attributes.fd[i],
            size > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(size),
            attributes.offset[i], attributes.stride[i],
            static_cast<uint32_t>(i));
      }
      const timespec& when = event->when;
      const uint64_t seconds = when.tv_sec < 0 ? 0 : when.tv_sec;
      session->sec_hi = seconds >> 32;
      session->sec_lo = seconds & 0xffffffffU;
      session->nsec =
          when.tv_nsec < 0 ? 0 : static_cast<uint32_t>(when.tv_nsec);
      session->awaiting_ack = true;
      treeland_capture_session_v1_send_ready(session->resource, session->sec_hi,
                                             session->sec_lo, session->nsec);
    }
    static void OnOutputDestroy(Session* session, void*) {
      session->commit.Disconnect();
      session->destroy.Disconnect();
      session->output = nullptr;
      session->output_locked = false;
      session->cursor_locked = false;
      session->started = false;
      treeland_capture_session_v1_send_cancel(
          session->resource,
          TREELAND_CAPTURE_SESSION_V1_CANCEL_REASON_PERMANENT);
    }
    void Stop() {
      commit.Disconnect();
      destroy.Disconnect();
      if (output != nullptr) {
        if (output_locked) wlr_output_lock_attach_render(output, false);
        if (cursor_locked) wlr_output_lock_software_cursors(output, false);
      }
      output_locked = false;
      cursor_locked = false;
      started = false;
      awaiting_ack = false;
      if (buffer != nullptr) {
        wlr_buffer_unlock(buffer);
        buffer = nullptr;
      }
    }
  };

  struct Context {
    Context(CaptureGlobal* new_global, wl_resource* new_resource)
        : global(new_global), resource(new_resource) {}

    CaptureGlobal* global;
    wl_resource* resource;
    wlr_output* output = nullptr;
    wlr_box layout_box = {};
    wlr_box source_box = {};
    Frame* frame = nullptr;
    Session* session = nullptr;
    uint32_t source_type = TREELAND_CAPTURE_CONTEXT_V1_SOURCE_TYPE_OUTPUT;
    bool capture_requested = false;
    bool selected = false;
    bool selecting = false;
    bool with_cursor = false;
    utils::SignalListener<Context, void> output_destroy{this, OnOutputDestroy};

    static void OnOutputDestroy(Context* context, void*) {
      context->output_destroy.Disconnect();
      context->output = nullptr;
      if (context->resource != nullptr) {
        treeland_capture_context_v1_send_source_failed(
            context->resource,
            TREELAND_CAPTURE_CONTEXT_V1_SOURCE_FAILURE_SOURCE_DESTROYED);
      }
    }
  };

  CaptureGlobal(TreelandProtocolManagerImpl* owner, wl_display* display)
      : owner_(owner) {
    if (display != nullptr) {
      global_ =
          wl_global_create(display, &treeland_capture_manager_v1_interface,
                           kVersion, this, Bind);
    }
  }
  ~CaptureGlobal() override {
    if (global_ != nullptr) wl_global_destroy(global_);
  }
  bool IsValid() const override { return global_ != nullptr; }

 private:
  static void DestroyRequest(wl_client*, wl_resource* resource) {
    wl_resource_destroy(resource);
  }
  static void Bind(wl_client* client, void* data, uint32_t version,
                   uint32_t id) {
    static const struct treeland_capture_manager_v1_interface impl = {
        .destroy = DestroyRequest,
        .get_context = GetContext,
    };
    wl_resource* resource =
        wl_resource_create(client, &treeland_capture_manager_v1_interface,
                           static_cast<int>(std::min(version, kVersion)), id);
    if (resource == nullptr) {
      wl_client_post_no_memory(client);
      return;
    }
    wl_resource_set_implementation(resource, &impl, data, nullptr);
  }
  static void GetContext(wl_client* client, wl_resource* manager_resource,
                         uint32_t id) {
    static const struct treeland_capture_context_v1_interface impl = {
        .destroy = DestroyRequest,
        .select_source = SelectSource,
        .capture = Capture,
        .create_session = CreateSession,
    };
    auto* global = static_cast<CaptureGlobal*>(
        wl_resource_get_user_data(manager_resource));
    wl_resource* resource = wl_resource_create(
        client, &treeland_capture_context_v1_interface, 1, id);
    if (resource == nullptr) {
      wl_client_post_no_memory(client);
      return;
    }
    auto* context = new Context(global, resource);
    global->contexts_.push_back(context);
    wl_resource_set_implementation(resource, &impl, context, DestroyContext);
  }
  static void DestroyContext(wl_resource* resource) {
    auto* context = static_cast<Context*>(wl_resource_get_user_data(resource));
    if (context == nullptr) return;
    if (context->selecting && context->global->owner_->Selector() != nullptr) {
      context->selecting = false;
      context->global->owner_->Selector()->Cancel(false);
    }
    context->resource = nullptr;
    context->output_destroy.Disconnect();
    if (context->frame != nullptr && context->frame->resource != nullptr) {
      wl_resource_destroy(context->frame->resource);
    }
    if (context->session != nullptr && context->session->resource != nullptr) {
      wl_resource_destroy(context->session->resource);
    }
    std::erase(context->global->contexts_, context);
    delete context;
  }
  static bool CompleteSelection(Context* context,
                                const view::WindowSelector::Target& target,
                                uint32_t source_type) {
    if (context == nullptr || context->resource == nullptr ||
        target.output == nullptr || target.box.width <= 0 ||
        target.box.height <= 0) {
      return false;
    }
    wlr_box output_box = {};
    wlr_output_layout_get_box(context->global->owner_->OutputLayout(),
                              target.output, &output_box);
    wlr_box clipped = {};
    if (!wlr_box_intersection(&clipped, &target.box, &output_box)) return false;

    int effective_width = 0;
    int effective_height = 0;
    wlr_output_effective_resolution(target.output, &effective_width,
                                    &effective_height);
    wlr_box logical = {
        .x = clipped.x - output_box.x,
        .y = clipped.y - output_box.y,
        .width = clipped.width,
        .height = clipped.height,
    };
    if (logical.x < 0 || logical.y < 0 || logical.width <= 0 ||
        logical.height <= 0 || logical.x + logical.width > effective_width ||
        logical.y + logical.height > effective_height) {
      return false;
    }
    wlr_box physical = {};
    wlr_box_transform(&physical, &logical,
                      wlr_output_transform_invert(target.output->transform),
                      effective_width, effective_height);
    const double scale = static_cast<double>(target.output->scale);
    physical.x =
        static_cast<int>(std::floor(static_cast<double>(physical.x) * scale));
    physical.y =
        static_cast<int>(std::floor(static_cast<double>(physical.y) * scale));
    physical.width = static_cast<int>(
        std::ceil(static_cast<double>(physical.width) * scale));
    physical.height = static_cast<int>(
        std::ceil(static_cast<double>(physical.height) * scale));
    const wlr_box buffer_bounds = {
        .x = 0,
        .y = 0,
        .width = target.output->width,
        .height = target.output->height,
    };
    wlr_box bounded = {};
    if (!wlr_box_intersection(&bounded, &physical, &buffer_bounds)) {
      return false;
    }

    context->output_destroy.Disconnect();
    context->output = target.output;
    context->layout_box = clipped;
    context->source_box = bounded;
    context->source_type = source_type;
    context->selected = true;
    context->output_destroy.Connect(&context->output->events.destroy);
    treeland_capture_context_v1_send_source_ready(
        context->resource, logical.x, logical.y,
        static_cast<uint32_t>(logical.width),
        static_cast<uint32_t>(logical.height), source_type);
    return true;
  }
  static void SelectSource(wl_client*, wl_resource* resource, uint32_t hint,
                           uint32_t, uint32_t with_cursor,
                           wl_resource* mask_resource) {
    auto* context = static_cast<Context*>(wl_resource_get_user_data(resource));
    if (!context->global->owner_->CaptureAllowed() || context->selected) {
      treeland_capture_context_v1_send_source_failed(
          resource, TREELAND_CAPTURE_CONTEXT_V1_SOURCE_FAILURE_OTHER);
      return;
    }
    if (context->selecting) {
      treeland_capture_context_v1_send_source_failed(
          resource, TREELAND_CAPTURE_CONTEXT_V1_SOURCE_FAILURE_SELECTOR_BUSY);
      return;
    }
    context->with_cursor = with_cursor != 0;
    const uint32_t normalized_hint =
        hint == 0 ? TREELAND_CAPTURE_CONTEXT_V1_SOURCE_TYPE_OUTPUT |
                        TREELAND_CAPTURE_CONTEXT_V1_SOURCE_TYPE_WINDOW |
                        TREELAND_CAPTURE_CONTEXT_V1_SOURCE_TYPE_REGION
                  : hint;
    view::WindowSelector* selector = context->global->owner_->Selector();
    if (selector == nullptr) {
      treeland_capture_context_v1_send_source_failed(
          resource, TREELAND_CAPTURE_CONTEXT_V1_SOURCE_FAILURE_OTHER);
      return;
    }
    uint32_t allowed_modes = 0;
    if ((normalized_hint & TREELAND_CAPTURE_CONTEXT_V1_SOURCE_TYPE_OUTPUT) !=
        0) {
      allowed_modes |=
          view::WindowSelector::ModeMask(view::WindowSelector::Mode::kOutput);
    }
    if ((normalized_hint & TREELAND_CAPTURE_CONTEXT_V1_SOURCE_TYPE_WINDOW) !=
        0) {
      allowed_modes |=
          view::WindowSelector::ModeMask(view::WindowSelector::Mode::kWindow);
    }
    if ((normalized_hint & TREELAND_CAPTURE_CONTEXT_V1_SOURCE_TYPE_REGION) !=
        0) {
      allowed_modes |=
          view::WindowSelector::ModeMask(view::WindowSelector::Mode::kRegion);
    }
    wlr_surface* mask = mask_resource == nullptr
                            ? nullptr
                            : wlr_surface_from_resource(mask_resource);
    if (!selector->Start(
            allowed_modes, mask,
            [context](
                std::optional<view::WindowSelector::Selection> selection) {
              context->selecting = false;
              if (!selection.has_value()) {
                treeland_capture_context_v1_send_source_failed(
                    context->resource,
                    TREELAND_CAPTURE_CONTEXT_V1_SOURCE_FAILURE_USER_CANCEL);
                return;
              }
              uint32_t source_type = 0;
              switch (selection->mode) {
                case view::WindowSelector::Mode::kOutput:
                  source_type = TREELAND_CAPTURE_CONTEXT_V1_SOURCE_TYPE_OUTPUT;
                  break;
                case view::WindowSelector::Mode::kWindow:
                  source_type = TREELAND_CAPTURE_CONTEXT_V1_SOURCE_TYPE_WINDOW;
                  break;
                case view::WindowSelector::Mode::kRegion:
                  source_type = TREELAND_CAPTURE_CONTEXT_V1_SOURCE_TYPE_REGION;
                  break;
              }
              if (!CompleteSelection(context, selection->target, source_type)) {
                treeland_capture_context_v1_send_source_failed(
                    context->resource,
                    TREELAND_CAPTURE_CONTEXT_V1_SOURCE_FAILURE_OTHER);
              }
            })) {
      treeland_capture_context_v1_send_source_failed(
          resource, TREELAND_CAPTURE_CONTEXT_V1_SOURCE_FAILURE_SELECTOR_BUSY);
      return;
    }
    context->selecting = true;
  }
  static void Capture(wl_client* client, wl_resource* resource, uint32_t id) {
    static const struct treeland_capture_frame_v1_interface impl = {
        .destroy = DestroyRequest,
        .copy = CopyFrame,
    };
    auto* context = static_cast<Context*>(wl_resource_get_user_data(resource));
    wl_resource* frame_resource =
        wl_resource_create(client, &treeland_capture_frame_v1_interface, 1, id);
    if (frame_resource == nullptr) {
      wl_client_post_no_memory(client);
      return;
    }
    auto* frame = new Frame(context->global, context, frame_resource,
                            context->output, context->source_box);
    context->global->frames_.push_back(frame);
    wl_resource_set_implementation(frame_resource, &impl, frame, DestroyFrame);
    if (context->capture_requested) {
      // The protocol permits only one one-shot capture per context. Keep the
      // rejected child independent so destroying its context cannot leave a
      // dangling back-pointer.
      frame->context = nullptr;
      frame->output = nullptr;
      frame->Fail();
      return;
    }
    context->capture_requested = true;
    context->frame = frame;
    if (!context->selected || frame->output == nullptr ||
        !context->global->owner_->CaptureAllowed()) {
      frame->Fail();
      return;
    }
    frame->commit.Connect(&frame->output->events.commit);
    frame->destroy.Connect(&frame->output->events.destroy);
    wlr_output_lock_attach_render(frame->output, true);
    frame->output_locked = true;
    wlr_output_schedule_frame(frame->output);
  }
  // Resource order is fixed by the generated Wayland ABI.
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  static void CopyFrame(wl_client*, wl_resource* resource,
                        wl_resource* buffer_resource) {
    auto* frame = static_cast<Frame*>(wl_resource_get_user_data(resource));
    if (frame->failed || frame->source == nullptr) {
      frame->Fail();
      return;
    }
    wlr_buffer* destination = wlr_buffer_try_from_resource(buffer_resource);
    if (destination == nullptr ||
        destination->width < frame->source_box.width ||
        destination->height < frame->source_box.height) {
      frame->Fail();
      return;
    }
    wlr_texture* texture = wlr_texture_from_buffer(
        frame->global->owner_->Renderer(), frame->source);
    void* data = nullptr;
    uint32_t format = 0;
    size_t stride = 0;
    if (texture == nullptr || !wlr_buffer_begin_data_ptr_access(
                                  destination, WLR_BUFFER_DATA_PTR_ACCESS_WRITE,
                                  &data, &format, &stride)) {
      if (texture != nullptr) wlr_texture_destroy(texture);
      frame->Fail();
      return;
    }
    const wlr_texture_read_pixels_options options = {
        .data = data,
        .format = format,
        .stride = static_cast<uint32_t>(stride),
        .src_box = frame->source_box,
    };
    const bool copied = wlr_texture_read_pixels(texture, &options);
    wlr_buffer_end_data_ptr_access(destination);
    wlr_texture_destroy(texture);
    if (copied) {
      treeland_capture_frame_v1_send_flags(resource, 0);
      treeland_capture_frame_v1_send_ready(resource);
    } else {
      frame->Fail();
    }
  }
  static void DestroyFrame(wl_resource* resource) {
    auto* frame = static_cast<Frame*>(wl_resource_get_user_data(resource));
    if (frame == nullptr) return;
    frame->resource = nullptr;
    frame->commit.Disconnect();
    frame->destroy.Disconnect();
    frame->UnlockOutput();
    if (frame->source != nullptr) wlr_buffer_unlock(frame->source);
    if (frame->context != nullptr && frame->context->frame == frame) {
      frame->context->frame = nullptr;
    }
    std::erase(frame->global->frames_, frame);
    delete frame;
  }
  static void CreateSession(wl_client* client, wl_resource* resource,
                            uint32_t id) {
    static const struct treeland_capture_session_v1_interface impl = {
        .destroy = DestroyRequest,
        .start = StartSession,
        .frame_done = SessionFrameDone,
    };
    auto* context = static_cast<Context*>(wl_resource_get_user_data(resource));
    wl_resource* session_resource = wl_resource_create(
        client, &treeland_capture_session_v1_interface, 1, id);
    if (session_resource == nullptr) {
      wl_client_post_no_memory(client);
      return;
    }
    auto* session = new Session(context->global, context, session_resource,
                                context->output, context->source_box);
    context->global->sessions_.push_back(session);
    wl_resource_set_implementation(session_resource, &impl, session,
                                   DestroySession);
    if (context->session != nullptr) {
      // A context owns one persistent stream. Reject another live stream while
      // keeping the returned resource safe to destroy in either order.
      session->context = nullptr;
      session->output = nullptr;
      treeland_capture_session_v1_send_cancel(
          session_resource,
          TREELAND_CAPTURE_SESSION_V1_CANCEL_REASON_PERMANENT);
      return;
    }
    context->session = session;
  }
  static void StartSession(wl_client*, wl_resource* resource) {
    auto* session = static_cast<Session*>(wl_resource_get_user_data(resource));
    if (session->started) return;
    if (session->output == nullptr || session->context == nullptr ||
        !session->context->selected ||
        !session->global->owner_->CaptureAllowed()) {
      treeland_capture_session_v1_send_cancel(
          resource, TREELAND_CAPTURE_SESSION_V1_CANCEL_REASON_PERMANENT);
      return;
    }
    session->commit.Connect(&session->output->events.commit);
    session->destroy.Connect(&session->output->events.destroy);
    wlr_output_lock_attach_render(session->output, true);
    session->output_locked = true;
    if (session->context->with_cursor) {
      wlr_output_lock_software_cursors(session->output, true);
      session->cursor_locked = true;
    }
    session->started = true;
    wlr_output_schedule_frame(session->output);
  }
  static void SessionFrameDone(wl_client*, wl_resource* resource,
                               uint32_t sec_hi, uint32_t sec_lo,
                               uint32_t nsec) {
    auto* session = static_cast<Session*>(wl_resource_get_user_data(resource));
    if (!session->started || !session->awaiting_ack ||
        session->sec_hi != sec_hi || session->sec_lo != sec_lo ||
        session->nsec != nsec) {
      return;
    }
    session->awaiting_ack = false;
    if (session->output != nullptr) wlr_output_schedule_frame(session->output);
  }
  static void DestroySession(wl_resource* resource) {
    auto* session = static_cast<Session*>(wl_resource_get_user_data(resource));
    if (session == nullptr) return;
    session->resource = nullptr;
    session->Stop();
    if (session->context != nullptr && session->context->session == session) {
      session->context->session = nullptr;
    }
    std::erase(session->global->sessions_, session);
    delete session;
  }

  TreelandProtocolManagerImpl* owner_;
  wl_global* global_ = nullptr;
  std::vector<Context*> contexts_;
  std::vector<Frame*> frames_;
  std::vector<Session*> sessions_;
};

}  // namespace

std::unique_ptr<TreelandGlobal> CreateTreelandCaptureGlobal(
    TreelandProtocolManagerImpl* owner, wl_display* display) {
  return std::make_unique<CaptureGlobal>(owner, display);
}

}  // namespace protocol
}  // namespace flakewm
