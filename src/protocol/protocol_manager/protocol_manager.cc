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

#include "src/protocol/protocol_manager/protocol_manager.h"

#include <absl/log/absl_log.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

#include "src/core/compositor_private/compositor_private.h"

namespace flakewm {
namespace protocol {

class ProtocolManager::SessionLockState final {
 public:
  class OutputPresentation final {
   public:
    OutputPresentation(SessionLockState* lock, wlr_output* output)
        : lock_(lock),
          output_(output),
          minimum_commit_seq_(output->commit_seq + 1) {
      present_.Connect(&output_->events.present);
      destroy_.Connect(&output_->events.destroy);
    }

    ~OutputPresentation() = default;

    OutputPresentation(const OutputPresentation&) = delete;
    OutputPresentation& operator=(const OutputPresentation&) = delete;

    bool Presented() const { return presented_; }
    wlr_output* Output() const { return output_; }

   private:
    static void OnPresent(OutputPresentation* output,
                          wlr_output_event_present* event) {
      if (event->presented &&
          event->commit_seq >= output->minimum_commit_seq_) {
        output->presented_ = true;
        output->present_.Disconnect();
        output->lock_->MaybeSendLocked();
      }
    }

    static void OnDestroy(OutputPresentation* output, void*) {
      output->present_.Disconnect();
      output->destroy_.Disconnect();
      output->output_ = nullptr;
      output->presented_ = true;
      output->lock_->MaybeSendLocked();
    }

    SessionLockState* lock_;
    wlr_output* output_;
    uint32_t minimum_commit_seq_;
    bool presented_ = false;
    utils::SignalListener<OutputPresentation, wlr_output_event_present>
        present_{this, OnPresent};
    utils::SignalListener<OutputPresentation, void> destroy_{this, OnDestroy};
  };

  class Surface final {
   public:
    Surface(SessionLockState* lock, wlr_session_lock_surface_v1* handle)
        : lock_(lock), handle_(handle) {
      scene_tree_ = wlr_scene_subsurface_tree_create(
          lock_->manager_->session_lock_parent_, handle_->surface);
      if (scene_tree_ == nullptr) {
        return;
      }
      map_.Connect(&handle_->surface->events.map);
      destroy_.Connect(&handle_->events.destroy);
      output_commit_.Connect(&handle_->output->events.commit);
      Configure();
    }

    ~Surface() {
      map_.Disconnect();
      destroy_.Disconnect();
      output_commit_.Disconnect();
      if (scene_tree_ != nullptr) {
        wlr_scene_node_destroy(&scene_tree_->node);
      }
    }

    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;

    bool IsValid() const { return scene_tree_ != nullptr; }

    void Configure() {
      if (handle_ == nullptr || handle_->output == nullptr ||
          scene_tree_ == nullptr) {
        return;
      }
      int width = 0;
      int height = 0;
      wlr_output_effective_resolution(handle_->output, &width, &height);
      if (width > 0 && height > 0 &&
          (handle_->pending.width != static_cast<uint32_t>(width) ||
           handle_->pending.height != static_cast<uint32_t>(height))) {
        wlr_session_lock_surface_v1_configure(handle_,
                                              static_cast<uint32_t>(width),
                                              static_cast<uint32_t>(height));
      }
      wlr_box box = {};
      wlr_output_layout_get_box(lock_->manager_->output_layout_,
                                handle_->output, &box);
      wlr_scene_node_set_position(&scene_tree_->node, box.x, box.y);
    }

   private:
    static void OnMap(Surface* surface, void*) {
      if (surface->handle_ != nullptr) {
        surface->lock_->manager_->FocusSessionLockSurface(
            surface->handle_->surface);
      }
    }

    static void OnDestroy(Surface* surface, void*) {
      surface->map_.Disconnect();
      surface->destroy_.Disconnect();
      surface->output_commit_.Disconnect();
      if (surface->scene_tree_ != nullptr) {
        wlr_scene_node_destroy(&surface->scene_tree_->node);
        surface->scene_tree_ = nullptr;
      }
      surface->handle_ = nullptr;
      surface->lock_->RemoveSurface(surface);
    }

    static void OnOutputCommit(Surface* surface,
                               wlr_output_event_commit* event) {
      if ((event->state->committed &
           (WLR_OUTPUT_STATE_MODE | WLR_OUTPUT_STATE_SCALE |
            WLR_OUTPUT_STATE_TRANSFORM | WLR_OUTPUT_STATE_ENABLED)) != 0) {
        surface->lock_->manager_->UpdateSessionLockGeometry();
      }
    }

    SessionLockState* lock_;
    wlr_session_lock_surface_v1* handle_;
    wlr_scene_tree* scene_tree_ = nullptr;
    utils::SignalListener<Surface, void> map_{this, OnMap};
    utils::SignalListener<Surface, void> destroy_{this, OnDestroy};
    utils::SignalListener<Surface, wlr_output_event_commit> output_commit_{
        this, OnOutputCommit};
  };

  SessionLockState(ProtocolManager* manager, wlr_session_lock_v1* handle)
      : manager_(manager), handle_(handle) {
    new_surface_.Connect(&handle_->events.new_surface);
    unlock_.Connect(&handle_->events.unlock);
    destroy_.Connect(&handle_->events.destroy);
    for (const std::unique_ptr<core::CompositorPrivate::Output>& output :
         manager_->compositor_->outputs_) {
      AddOutput(output->handle);
    }
    MaybeSendLocked();
  }

  ~SessionLockState() {
    new_surface_.Disconnect();
    unlock_.Disconnect();
    destroy_.Disconnect();
    surfaces_.clear();
  }

  SessionLockState(const SessionLockState&) = delete;
  SessionLockState& operator=(const SessionLockState&) = delete;

  void ConfigureSurfaces() {
    for (const std::unique_ptr<Surface>& surface : surfaces_) {
      surface->Configure();
    }
  }

  void AddOutput(wlr_output* output) {
    if (locked_sent_ || output == nullptr || !output->enabled ||
        std::any_of(outputs_.begin(), outputs_.end(),
                    [output](const std::unique_ptr<OutputPresentation>& item) {
                      return item->Output() == output;
                    })) {
      return;
    }
    outputs_.push_back(std::make_unique<OutputPresentation>(this, output));
  }

 private:
  static void OnNewSurface(SessionLockState* lock,
                           wlr_session_lock_surface_v1* surface) {
    auto wrapper = std::make_unique<Surface>(lock, surface);
    if (!wrapper->IsValid()) {
      ABSL_LOG(ERROR) << "Failed to create a session-lock surface";
      return;
    }
    lock->surfaces_.push_back(std::move(wrapper));
  }

  static void OnUnlock(SessionLockState* lock, void*) {
    lock->unlocked_ = true;
    lock->manager_->UnlockSession();
  }

  static void OnDestroy(SessionLockState* lock, void*) {
    lock->new_surface_.Disconnect();
    lock->unlock_.Disconnect();
    lock->destroy_.Disconnect();
    lock->handle_ = nullptr;
    lock->surfaces_.clear();
    lock->manager_->FinishSessionLock(lock->unlocked_);
  }

  void RemoveSurface(Surface* surface) {
    std::erase_if(surfaces_, [surface](const std::unique_ptr<Surface>& item) {
      return item.get() == surface;
    });
  }

  void MaybeSendLocked() {
    if (locked_sent_ || handle_ == nullptr ||
        !std::all_of(outputs_.begin(), outputs_.end(),
                     [](const std::unique_ptr<OutputPresentation>& output) {
                       return output->Presented();
                     })) {
      return;
    }
    locked_sent_ = true;
    wlr_session_lock_v1_send_locked(handle_);
  }

  ProtocolManager* manager_;
  wlr_session_lock_v1* handle_;
  bool unlocked_ = false;
  bool locked_sent_ = false;
  std::vector<std::unique_ptr<OutputPresentation>> outputs_;
  std::vector<std::unique_ptr<Surface>> surfaces_;
  utils::SignalListener<SessionLockState, wlr_session_lock_surface_v1>
      new_surface_{this, OnNewSurface};
  utils::SignalListener<SessionLockState, void> unlock_{this, OnUnlock};
  utils::SignalListener<SessionLockState, void> destroy_{this, OnDestroy};
};

ProtocolManager::ProtocolManager(core::CompositorPrivate* compositor)
    : compositor_(compositor),
      touchpad_manager_(std::make_unique<input::TouchpadManager>()) {}

ProtocolManager::~ProtocolManager() = default;

bool ProtocolManager::Create(wl_display* display, wlr_backend* backend,
                             wlr_seat* seat, wlr_cursor* cursor,
                             wlr_scene_tree* drag_icon_parent,
                             wlr_scene_tree* session_lock_parent,
                             wlr_output_layout* output_layout) {
  backend_ = backend;
  seat_ = seat;
  cursor_ = cursor;
  drag_icon_parent_ = drag_icon_parent;
  session_lock_parent_ = session_lock_parent;
  output_layout_ = output_layout;

  // These protocols are self-contained once their globals are published.
  tearing_manager_ = wlr_tearing_control_manager_v1_create(display, 1);
  ext_foreign_list_ = wlr_ext_foreign_toplevel_list_v1_create(display, 1);
  foreign_capture_source_manager_ =
      wlr_ext_foreign_toplevel_image_capture_source_manager_v1_create(display,
                                                                      1);
  output_capture_source_manager_ =
      wlr_ext_output_image_capture_source_manager_v1_create(display, 1);
  image_copy_capture_manager_ =
      wlr_ext_image_copy_capture_manager_v1_create(display, 1);
  if (wlr_data_control_manager_v1_create(display) == nullptr ||
      wlr_export_dmabuf_manager_v1_create(display) == nullptr ||
      wlr_screencopy_manager_v1_create(display) == nullptr ||
      ext_foreign_list_ == nullptr ||
      foreign_capture_source_manager_ == nullptr ||
      output_capture_source_manager_ == nullptr ||
      image_copy_capture_manager_ == nullptr || tearing_manager_ == nullptr ||
      wlr_xdg_wm_dialog_v1_create(display, 1) == nullptr) {
    return false;
  }

  idle_notifier_ = wlr_idle_notifier_v1_create(display);
  idle_inhibit_manager_ = wlr_idle_inhibit_v1_create(display);
  shortcuts_manager_ = wlr_keyboard_shortcuts_inhibit_v1_create(display);
  pointer_constraints_ = wlr_pointer_constraints_v1_create(display);
  relative_pointer_manager_ = wlr_relative_pointer_manager_v1_create(display);
  pointer_gestures_ = wlr_pointer_gestures_v1_create(display);
  tablet_manager_ = wlr_tablet_v2_create(display);
  virtual_pointer_manager_ = wlr_virtual_pointer_manager_v1_create(display);
  transient_seat_manager_ = wlr_transient_seat_manager_v1_create(display);
  activation_manager_ = wlr_xdg_activation_v1_create(display);
  foreign_manager_ = wlr_foreign_toplevel_manager_v1_create(display);
  output_manager_ = wlr_output_manager_v1_create(display);
  output_power_manager_ = wlr_output_power_manager_v1_create(display);
  session_lock_manager_ = wlr_session_lock_manager_v1_create(display);
  input_timestamps_ = std::make_unique<InputTimestampsManager>(display);
  toplevel_drag_manager_ = std::make_unique<ToplevelDragManager>(display);
  if (idle_notifier_ == nullptr || idle_inhibit_manager_ == nullptr ||
      shortcuts_manager_ == nullptr || pointer_constraints_ == nullptr ||
      relative_pointer_manager_ == nullptr || pointer_gestures_ == nullptr ||
      tablet_manager_ == nullptr || virtual_pointer_manager_ == nullptr ||
      transient_seat_manager_ == nullptr || activation_manager_ == nullptr ||
      foreign_manager_ == nullptr || output_manager_ == nullptr ||
      output_power_manager_ == nullptr || session_lock_manager_ == nullptr ||
      session_lock_parent_ == nullptr || !input_timestamps_->IsValid() ||
      !toplevel_drag_manager_->IsValid()) {
    return false;
  }

  new_virtual_pointer_.Connect(
      &virtual_pointer_manager_->events.new_virtual_pointer);
  new_constraint_.Connect(&pointer_constraints_->events.new_constraint);
  new_session_lock_.Connect(&session_lock_manager_->events.new_lock);
  toplevel_capture_request_.Connect(
      &foreign_capture_source_manager_->events.new_request);
  new_idle_inhibitor_.Connect(&idle_inhibit_manager_->events.new_inhibitor);
  new_shortcuts_inhibitor_.Connect(&shortcuts_manager_->events.new_inhibitor);
  create_transient_seat_.Connect(&transient_seat_manager_->events.create_seat);
  request_activate_.Connect(&activation_manager_->events.request_activate);
  output_apply_.Connect(&output_manager_->events.apply);
  output_test_.Connect(&output_manager_->events.test);
  output_power_.Connect(&output_power_manager_->events.set_mode);
  request_start_drag_.Connect(&seat_->events.request_start_drag);
  start_drag_.Connect(&seat_->events.start_drag);
  swipe_begin_.Connect(&cursor_->events.swipe_begin);
  swipe_update_.Connect(&cursor_->events.swipe_update);
  swipe_end_.Connect(&cursor_->events.swipe_end);
  pinch_begin_.Connect(&cursor_->events.pinch_begin);
  pinch_update_.Connect(&cursor_->events.pinch_update);
  pinch_end_.Connect(&cursor_->events.pinch_end);
  hold_begin_.Connect(&cursor_->events.hold_begin);
  hold_end_.Connect(&cursor_->events.hold_end);
  tablet_axis_.Connect(&cursor_->events.tablet_tool_axis);
  tablet_proximity_.Connect(&cursor_->events.tablet_tool_proximity);
  tablet_tip_.Connect(&cursor_->events.tablet_tool_tip);
  tablet_button_.Connect(&cursor_->events.tablet_tool_button);
  return true;
}

void ProtocolManager::AddInput(wlr_input_device* device) {
  touchpad_manager_->AddDevice(device);
  if (device->type == WLR_INPUT_DEVICE_TABLET) {
    wlr_tablet* tablet = wlr_tablet_from_input_device(device);
    wlr_tablet_v2_tablet* protocol_tablet =
        wlr_tablet_create(tablet_manager_, seat_, device);
    if (protocol_tablet != nullptr) {
      tablets_[tablet] = protocol_tablet;
    }
    wlr_cursor_attach_input_device(cursor_, device);
    return;
  }
  if (device->type == WLR_INPUT_DEVICE_TABLET_PAD) {
    wlr_tablet_pad_create(tablet_manager_, seat_, device);
  }
}

void ProtocolManager::AddOutput(wlr_output* output) {
  if (session_lock_ != nullptr) {
    session_lock_->AddOutput(output);
  }
  UpdateOutputs();
}

void ProtocolManager::UpdateOutputs() {
  if (output_manager_ == nullptr) {
    return;
  }
  wlr_output_configuration_v1* configuration =
      wlr_output_configuration_v1_create();
  if (configuration == nullptr) {
    return;
  }
  for (const std::unique_ptr<core::CompositorPrivate::Output>& output :
       compositor_->outputs_) {
    if (output->handle == nullptr) {
      continue;
    }
    wlr_output_configuration_head_v1* head =
        wlr_output_configuration_head_v1_create(configuration, output->handle);
    if (head == nullptr) {
      wlr_output_configuration_v1_destroy(configuration);
      return;
    }
    wlr_box box = {};
    wlr_output_layout_get_box(output_layout_, output->handle, &box);
    head->state.x = box.x;
    head->state.y = box.y;
  }
  wlr_output_manager_v1_set_configuration(output_manager_, configuration);
  UpdateSessionLockGeometry();
}

void ProtocolManager::NotifyKeyboard(uint32_t time_msec) {
  UpdateIdleInhibition();
  wlr_idle_notifier_v1_notify_activity(idle_notifier_, seat_);
  wl_client* client =
      seat_->keyboard_state.focused_surface == nullptr
          ? nullptr
          : wl_resource_get_client(
                seat_->keyboard_state.focused_surface->resource);
  input_timestamps_->SendKeyboard(client, time_msec);
}

void ProtocolManager::NotifyPointer(uint32_t time_msec) {
  UpdateIdleInhibition();
  wlr_idle_notifier_v1_notify_activity(idle_notifier_, seat_);
  wl_client* client = seat_->pointer_state.focused_surface == nullptr
                          ? nullptr
                          : wl_resource_get_client(
                                seat_->pointer_state.focused_surface->resource);
  input_timestamps_->SendPointer(client, time_msec);
}

void ProtocolManager::SendRelativeMotion(
    const wlr_pointer_motion_event& event) {
  if (relative_pointer_manager_ == nullptr || seat_ == nullptr) {
    return;
  }
  wlr_relative_pointer_manager_v1_send_relative_motion(
      relative_pointer_manager_, seat_,
      static_cast<uint64_t>(event.time_msec) * 1000, event.delta_x,
      event.delta_y, event.unaccel_dx, event.unaccel_dy);
}

void ProtocolManager::NotifyTouch(wlr_surface* surface, uint32_t time_msec) {
  UpdateIdleInhibition();
  wlr_idle_notifier_v1_notify_activity(idle_notifier_, seat_);
  wl_client* client =
      surface == nullptr ? nullptr : wl_resource_get_client(surface->resource);
  input_timestamps_->SendTouch(client, time_msec);
}

bool ProtocolManager::ShouldForwardAxis(
    const wlr_pointer_axis_event& event) const {
  return touchpad_manager_->ShouldForwardAxis(event);
}

bool ProtocolManager::WantsTearing(wlr_surface* surface) const {
  return surface != nullptr &&
         wlr_tearing_control_manager_v1_surface_hint_from_surface(
             tearing_manager_, surface) ==
             WP_TEARING_CONTROL_V1_PRESENTATION_HINT_ASYNC;
}

bool ProtocolManager::ShortcutsInhibited() const {
  if (shortcuts_manager_ == nullptr || seat_ == nullptr) {
    return false;
  }
  wlr_keyboard_shortcuts_inhibitor_v1* inhibitor;
  wl_list_for_each(inhibitor, &shortcuts_manager_->inhibitors, link) {
    if (inhibitor->seat == seat_ && inhibitor->active) {
      return true;
    }
  }
  return false;
}

bool ProtocolManager::SessionLocked() const { return session_locked_; }

bool ProtocolManager::ConfinePointer(double* delta_x, double* delta_y) const {
  if (active_constraint_ == nullptr ||
      seat_->pointer_state.focused_surface != active_constraint_->surface) {
    return true;
  }
  if (active_constraint_->type == WLR_POINTER_CONSTRAINT_V1_LOCKED) {
    *delta_x = 0;
    *delta_y = 0;
    return true;
  }

  pixman_region32_t full_surface = {};
  pixman_region32_t* region = &active_constraint_->region;
  const pixman_box32_t* extents = pixman_region32_extents(region);
  if (extents->x1 == extents->x2 || extents->y1 == extents->y2) {
    wlr_surface* surface = active_constraint_->surface;
    pixman_region32_init_rect(&full_surface, 0, 0, surface->current.width,
                              surface->current.height);
    region = &full_surface;
  }

  double confined_x = 0;
  double confined_y = 0;
  const double sx = seat_->pointer_state.sx;
  const double sy = seat_->pointer_state.sy;
  const bool confined = wlr_region_confine(
      region, sx, sy, sx + *delta_x, sy + *delta_y, &confined_x, &confined_y);
  if (region == &full_surface) {
    pixman_region32_fini(&full_surface);
  }
  if (!confined) {
    *delta_x = 0;
    *delta_y = 0;
    return false;
  }
  *delta_x = confined_x - sx;
  *delta_y = confined_y - sy;
  return true;
}

void ProtocolManager::UpdatePointerFocus(wlr_surface* surface) {
  wlr_pointer_constraint_v1* constraint =
      surface == nullptr ? nullptr
                         : wlr_pointer_constraints_v1_constraint_for_surface(
                               pointer_constraints_, surface, seat_);
  ActivateConstraint(constraint);
}

void ProtocolManager::UpdateKeyboardFocus(wlr_surface* surface) {
  wlr_keyboard_shortcuts_inhibitor_v1* inhibitor;
  wl_list_for_each(inhibitor, &shortcuts_manager_->inhibitors, link) {
    if (inhibitor->surface == surface) {
      wlr_keyboard_shortcuts_inhibitor_v1_activate(inhibitor);
    } else {
      wlr_keyboard_shortcuts_inhibitor_v1_deactivate(inhibitor);
    }
  }
  UpdateIdleInhibition();
}

void ProtocolManager::MoveDrag() {
  if (drag_icon_tree_ != nullptr) {
    wlr_scene_node_set_position(&drag_icon_tree_->node,
                                static_cast<int>(cursor_->x),
                                static_cast<int>(cursor_->y));
  }
  if (seat_->drag_source == nullptr) {
    return;
  }
  ToplevelDrag* drag = toplevel_drag_manager_->Find(seat_->drag_source);
  if (drag == nullptr || drag->Toplevel() == nullptr) {
    return;
  }
  core::CompositorPrivate::Toplevel* toplevel =
      compositor_->FindToplevel(drag->Toplevel());
  if (toplevel == nullptr || toplevel->scene_tree == nullptr) {
    return;
  }
  wlr_scene_node_set_position(&toplevel->scene_tree->node,
                              static_cast<int>(cursor_->x) - drag->OffsetX(),
                              static_cast<int>(cursor_->y) - drag->OffsetY());
}

void ProtocolManager::MapToplevel(wlr_surface* surface, const char* title,
                                  const char* app_id) {
  if (surface == nullptr || FindForeign(surface) != nullptr) {
    return;
  }
  auto foreign = std::make_unique<ForeignToplevel>(this, foreign_manager_,
                                                   ext_foreign_list_, surface);
  if (!foreign->IsValid()) {
    return;
  }
  foreign->SetTitle(title);
  foreign->SetAppId(app_id);
  foreign_toplevels_.push_back(std::move(foreign));
  UpdateToplevel(surface);
}

void ProtocolManager::UnmapToplevel(wlr_surface* surface) {
  for (auto iterator = foreign_toplevels_.begin();
       iterator != foreign_toplevels_.end(); ++iterator) {
    if ((*iterator)->Surface() == surface) {
      foreign_toplevels_.erase(iterator);
      UpdateIdleInhibition();
      return;
    }
  }
  UpdateIdleInhibition();
}

void ProtocolManager::UpdateToplevel(wlr_surface* surface) {
  ForeignToplevel* foreign = FindForeign(surface);
  core::CompositorPrivate::Toplevel* toplevel =
      compositor_->ToplevelForSurface(surface);
  if (foreign == nullptr || toplevel == nullptr) {
    return;
  }
  foreign->SetTitle(toplevel->Title());
  foreign->SetAppId(toplevel->AppId());
  foreign->SetMaximized(toplevel->maximized);
  foreign->SetMinimized(toplevel->minimized);
  foreign->SetFullscreen(toplevel->RequestedFullscreen());
  foreign->SetActivated(seat_->keyboard_state.focused_surface == surface);
  if (toplevel->scene_tree == nullptr) {
    foreign->SetOutput(nullptr);
    return;
  }
  const wlr_box frame = toplevel->FrameGeometry();
  foreign->SetOutput(wlr_output_layout_output_at(
      output_layout_,
      toplevel->scene_tree->node.x + frame.x + frame.width / 2.0,
      toplevel->scene_tree->node.y + frame.y + frame.height / 2.0));
}

void ProtocolManager::UpdateToplevelParent(wlr_surface* surface,
                                           wlr_surface* parent) {
  ForeignToplevel* foreign = FindForeign(surface);
  ForeignToplevel* foreign_parent = FindForeign(parent);
  if (foreign != nullptr) {
    foreign->SetParent(foreign_parent == nullptr ? nullptr
                                                 : foreign_parent->Handle());
  }
}

void ProtocolManager::RequestMaximize(wlr_surface* surface, bool maximized) {
  core::CompositorPrivate::Toplevel* toplevel =
      compositor_->ToplevelForSurface(surface);
  if (toplevel != nullptr && toplevel->CanManage()) {
    compositor_->SetMaximized(toplevel, maximized && toplevel->CanMaximize());
  }
}

void ProtocolManager::RequestMinimize(wlr_surface* surface, bool minimized) {
  core::CompositorPrivate::Toplevel* toplevel =
      compositor_->ToplevelForSurface(surface);
  if (toplevel == nullptr || !toplevel->CanMinimize()) {
    return;
  }
  if (minimized) {
    compositor_->Minimize(toplevel);
    return;
  }
  toplevel->minimized = false;
  toplevel->SetMinimizedState(false);
  if (toplevel->scene_tree != nullptr) {
    wlr_scene_node_set_enabled(&toplevel->scene_tree->node, true);
  }
}

void ProtocolManager::RequestActivate(wlr_surface* surface, wlr_seat* seat) {
  if (seat != seat_) {
    return;
  }
  compositor_->FocusToplevel(compositor_->ToplevelForSurface(surface));
}

void ProtocolManager::RequestFullscreen(wlr_surface* surface, bool fullscreen,
                                        wlr_output*) {
  core::CompositorPrivate::Toplevel* toplevel =
      compositor_->ToplevelForSurface(surface);
  if (toplevel != nullptr) {
    toplevel->SetFullscreenState(fullscreen);
    UpdateToplevel(surface);
  }
}

void ProtocolManager::RequestClose(wlr_surface* surface) {
  core::CompositorPrivate::Toplevel* toplevel =
      compositor_->ToplevelForSurface(surface);
  if (toplevel != nullptr) {
    toplevel->Close();
  }
}

void ProtocolManager::OnNewVirtualPointer(
    ProtocolManager* manager, wlr_virtual_pointer_v1_new_pointer_event* event) {
  if (event->suggested_seat == nullptr ||
      event->suggested_seat == manager->seat_) {
    wlr_cursor_attach_input_device(manager->cursor_,
                                   &event->new_pointer->pointer.base);
  }
}

void ProtocolManager::OnNewConstraint(ProtocolManager* manager,
                                      wlr_pointer_constraint_v1* constraint) {
  if (manager->seat_->pointer_state.focused_surface == constraint->surface &&
      constraint->seat == manager->seat_) {
    manager->ActivateConstraint(constraint);
  }
}

void ProtocolManager::OnNewSessionLock(ProtocolManager* manager,
                                       wlr_session_lock_v1* lock) {
  manager->BeginSessionLock(lock);
}

void ProtocolManager::OnToplevelCaptureRequest(
    ProtocolManager* manager,
    wlr_ext_foreign_toplevel_image_capture_source_manager_v1_request* request) {
  wlr_ext_image_capture_source_v1* source = nullptr;
  if (request->toplevel_handle != nullptr &&
      request->toplevel_handle->data != nullptr) {
    auto* foreign =
        static_cast<ForeignToplevel*>(request->toplevel_handle->data);
    core::CompositorPrivate::Toplevel* toplevel =
        manager->compositor_->ToplevelForSurface(foreign->Surface());
    if (toplevel != nullptr && toplevel->scene_tree != nullptr) {
      source = foreign->CaptureSource(
          &toplevel->scene_tree->node,
          wl_display_get_event_loop(manager->seat_->display),
          manager->compositor_->allocator_, manager->compositor_->renderer_);
    }
  }
  if (!wlr_ext_foreign_toplevel_image_capture_source_manager_v1_request_accept(
          request, source)) {
    ABSL_LOG(ERROR) << "Failed to create a toplevel capture source resource";
  }
}

void ProtocolManager::OnConstraintDestroy(ProtocolManager* manager, void*) {
  manager->constraint_destroy_.Disconnect();
  manager->active_constraint_ = nullptr;
}

void ProtocolManager::OnNewIdleInhibitor(ProtocolManager* manager,
                                         wlr_idle_inhibitor_v1*) {
  manager->UpdateIdleInhibition();
}

void ProtocolManager::OnNewShortcutsInhibitor(
    ProtocolManager* manager, wlr_keyboard_shortcuts_inhibitor_v1* inhibitor) {
  if (inhibitor->seat == manager->seat_ &&
      inhibitor->surface == manager->seat_->keyboard_state.focused_surface) {
    wlr_keyboard_shortcuts_inhibitor_v1_activate(inhibitor);
  }
}

void ProtocolManager::OnCreateTransientSeat(
    ProtocolManager* manager, wlr_transient_seat_v1* transient_seat) {
  static unsigned int serial = 0;
  const std::string name = "transient-" + std::to_string(++serial);
  wlr_seat* seat = wlr_seat_create(manager->seat_->display, name.c_str());
  if (seat == nullptr) {
    wlr_transient_seat_v1_deny(transient_seat);
    return;
  }
  wlr_transient_seat_v1_ready(transient_seat, seat);
}

void ProtocolManager::OnRequestActivate(
    ProtocolManager* manager,
    wlr_xdg_activation_v1_request_activate_event* event) {
  manager->compositor_->FocusToplevel(
      manager->compositor_->ToplevelForSurface(event->surface));
}

void ProtocolManager::OnOutputApply(
    ProtocolManager* manager, wlr_output_configuration_v1* configuration) {
  manager->ApplyOutputConfiguration(configuration, false);
}

void ProtocolManager::OnOutputTest(ProtocolManager* manager,
                                   wlr_output_configuration_v1* configuration) {
  manager->ApplyOutputConfiguration(configuration, true);
}

void ProtocolManager::OnOutputPower(ProtocolManager* manager,
                                    wlr_output_power_v1_set_mode_event* event) {
  wlr_output_state state = {};
  wlr_output_state_init(&state);
  wlr_output_state_set_enabled(&state,
                               event->mode == ZWLR_OUTPUT_POWER_V1_MODE_ON);
  if (!wlr_output_commit_state(event->output, &state)) {
    ABSL_LOG(WARNING) << "Output rejected a power state change";
  }
  wlr_output_state_finish(&state);
  manager->UpdateOutputs();
}

void ProtocolManager::OnRequestStartDrag(
    ProtocolManager* manager, wlr_seat_request_start_drag_event* event) {
  if (wlr_seat_validate_pointer_grab_serial(manager->seat_, event->origin,
                                            event->serial)) {
    wlr_seat_start_pointer_drag(manager->seat_, event->drag, event->serial);
    return;
  }
  wlr_touch_point* point = nullptr;
  if (wlr_seat_validate_touch_grab_serial(manager->seat_, event->origin,
                                          event->serial, &point)) {
    wlr_seat_start_touch_drag(manager->seat_, event->drag, event->serial,
                              point);
    return;
  }
  wlr_data_source_destroy(event->drag->source);
}

void ProtocolManager::OnStartDrag(ProtocolManager* manager, wlr_drag* drag) {
  manager->drag_destroy_.Connect(&drag->events.destroy);
  if (drag->icon != nullptr) {
    manager->drag_icon_tree_ =
        wlr_scene_drag_icon_create(manager->drag_icon_parent_, drag->icon);
    manager->MoveDrag();
  }
}

void ProtocolManager::OnDragDestroy(ProtocolManager* manager, void*) {
  manager->drag_destroy_.Disconnect();
  manager->drag_icon_tree_ = nullptr;
}

void ProtocolManager::OnSwipeBegin(ProtocolManager* manager,
                                   wlr_pointer_swipe_begin_event* event) {
  manager->NotifyPointer(event->time_msec);
  manager->touchpad_manager_->BeginSwipe(event->fingers);
  wlr_pointer_gestures_v1_send_swipe_begin(manager->pointer_gestures_,
                                           manager->seat_, event->time_msec,
                                           event->fingers);
}

void ProtocolManager::OnSwipeUpdate(ProtocolManager* manager,
                                    wlr_pointer_swipe_update_event* event) {
  manager->NotifyPointer(event->time_msec);
  manager->touchpad_manager_->UpdateSwipe(event->dx, event->dy);
  wlr_pointer_gestures_v1_send_swipe_update(manager->pointer_gestures_,
                                            manager->seat_, event->time_msec,
                                            event->dx, event->dy);
}

void ProtocolManager::OnSwipeEnd(ProtocolManager* manager,
                                 wlr_pointer_swipe_end_event* event) {
  manager->NotifyPointer(event->time_msec);
  const bool handled = manager->touchpad_manager_->EndSwipe(event->cancelled);
  wlr_pointer_gestures_v1_send_swipe_end(manager->pointer_gestures_,
                                         manager->seat_, event->time_msec,
                                         event->cancelled || handled);
}

void ProtocolManager::OnPinchBegin(ProtocolManager* manager,
                                   wlr_pointer_pinch_begin_event* event) {
  manager->NotifyPointer(event->time_msec);
  manager->touchpad_manager_->BeginPinch(event->fingers);
  wlr_pointer_gestures_v1_send_pinch_begin(manager->pointer_gestures_,
                                           manager->seat_, event->time_msec,
                                           event->fingers);
}

void ProtocolManager::OnPinchUpdate(ProtocolManager* manager,
                                    wlr_pointer_pinch_update_event* event) {
  manager->NotifyPointer(event->time_msec);
  manager->touchpad_manager_->UpdatePinch(*event);
  wlr_pointer_gestures_v1_send_pinch_update(
      manager->pointer_gestures_, manager->seat_, event->time_msec, event->dx,
      event->dy, event->scale, event->rotation);
}

void ProtocolManager::OnPinchEnd(ProtocolManager* manager,
                                 wlr_pointer_pinch_end_event* event) {
  manager->NotifyPointer(event->time_msec);
  const bool handled = manager->touchpad_manager_->EndPinch(event->cancelled);
  wlr_pointer_gestures_v1_send_pinch_end(manager->pointer_gestures_,
                                         manager->seat_, event->time_msec,
                                         event->cancelled || handled);
}

void ProtocolManager::OnHoldBegin(ProtocolManager* manager,
                                  wlr_pointer_hold_begin_event* event) {
  manager->NotifyPointer(event->time_msec);
  manager->touchpad_manager_->BeginHold(event->fingers);
  wlr_pointer_gestures_v1_send_hold_begin(manager->pointer_gestures_,
                                          manager->seat_, event->time_msec,
                                          event->fingers);
}

void ProtocolManager::OnHoldEnd(ProtocolManager* manager,
                                wlr_pointer_hold_end_event* event) {
  manager->NotifyPointer(event->time_msec);
  const bool handled = manager->touchpad_manager_->EndHold(event->cancelled);
  wlr_pointer_gestures_v1_send_hold_end(manager->pointer_gestures_,
                                        manager->seat_, event->time_msec,
                                        event->cancelled || handled);
}

void ProtocolManager::OnTabletAxis(ProtocolManager* manager,
                                   wlr_tablet_tool_axis_event* event) {
  manager->NotifyPointer(event->time_msec);
  if (event->updated_axes & (WLR_TABLET_TOOL_AXIS_X | WLR_TABLET_TOOL_AXIS_Y)) {
    wlr_cursor_warp_absolute(manager->cursor_, &event->tablet->base, event->x,
                             event->y);
    manager->TabletMotion(event->tool, manager->cursor_->x,
                          manager->cursor_->y);
  }
  wlr_tablet_v2_tablet_tool* tool = manager->TabletTool(event->tool);
  if (tool == nullptr) {
    return;
  }
  if (event->updated_axes & WLR_TABLET_TOOL_AXIS_PRESSURE) {
    wlr_tablet_v2_tablet_tool_notify_pressure(tool, event->pressure);
  }
  if (event->updated_axes & WLR_TABLET_TOOL_AXIS_DISTANCE) {
    wlr_tablet_v2_tablet_tool_notify_distance(tool, event->distance);
  }
  if (event->updated_axes &
      (WLR_TABLET_TOOL_AXIS_TILT_X | WLR_TABLET_TOOL_AXIS_TILT_Y)) {
    wlr_tablet_v2_tablet_tool_notify_tilt(tool, event->tilt_x, event->tilt_y);
  }
  if (event->updated_axes & WLR_TABLET_TOOL_AXIS_ROTATION) {
    wlr_tablet_v2_tablet_tool_notify_rotation(tool, event->rotation);
  }
  if (event->updated_axes & WLR_TABLET_TOOL_AXIS_SLIDER) {
    wlr_tablet_v2_tablet_tool_notify_slider(tool, event->slider);
  }
  if (event->updated_axes & WLR_TABLET_TOOL_AXIS_WHEEL) {
    wlr_tablet_v2_tablet_tool_notify_wheel(tool, event->wheel_delta, 0);
  }
}

void ProtocolManager::OnTabletProximity(
    ProtocolManager* manager, wlr_tablet_tool_proximity_event* event) {
  manager->NotifyPointer(event->time_msec);
  wlr_tablet_v2_tablet_tool* tool = manager->TabletTool(event->tool);
  if (tool == nullptr) {
    return;
  }
  if (event->state == WLR_TABLET_TOOL_PROXIMITY_OUT) {
    wlr_tablet_v2_tablet_tool_notify_proximity_out(tool);
    return;
  }
  wlr_cursor_warp_absolute(manager->cursor_, &event->tablet->base, event->x,
                           event->y);
  double sx = 0;
  double sy = 0;
  wlr_surface* surface = nullptr;
  manager->compositor_->ToplevelAt(manager->cursor_->x, manager->cursor_->y,
                                   &surface, &sx, &sy);
  auto tablet = manager->tablets_.find(event->tablet);
  if (surface != nullptr && tablet != manager->tablets_.end()) {
    wlr_tablet_v2_tablet_tool_notify_proximity_in(tool, tablet->second,
                                                  surface);
    wlr_tablet_v2_tablet_tool_notify_motion(tool, sx, sy);
  }
}

void ProtocolManager::OnTabletTip(ProtocolManager* manager,
                                  wlr_tablet_tool_tip_event* event) {
  manager->NotifyPointer(event->time_msec);
  wlr_cursor_warp_absolute(manager->cursor_, &event->tablet->base, event->x,
                           event->y);
  manager->TabletMotion(event->tool, manager->cursor_->x, manager->cursor_->y);
  wlr_tablet_v2_tablet_tool* tool = manager->TabletTool(event->tool);
  if (tool == nullptr) {
    return;
  }
  if (event->state == WLR_TABLET_TOOL_TIP_DOWN) {
    wlr_tablet_v2_tablet_tool_notify_down(tool);
  } else {
    wlr_tablet_v2_tablet_tool_notify_up(tool);
  }
}

void ProtocolManager::OnTabletButton(ProtocolManager* manager,
                                     wlr_tablet_tool_button_event* event) {
  manager->NotifyPointer(event->time_msec);
  wlr_tablet_v2_tablet_tool* tool = manager->TabletTool(event->tool);
  if (tool == nullptr) {
    return;
  }
  wlr_tablet_v2_tablet_tool_notify_button(
      tool, event->button,
      static_cast<zwp_tablet_pad_v2_button_state>(event->state));
}

void ProtocolManager::ApplyOutputConfiguration(
    wlr_output_configuration_v1* configuration, bool test_only) {
  size_t states_length = 0;
  wlr_backend_output_state* states =
      wlr_output_configuration_v1_build_state(configuration, &states_length);
  bool accepted =
      states != nullptr && wlr_backend_test(backend_, states, states_length);
  if (accepted && !test_only) {
    accepted = wlr_backend_commit(backend_, states, states_length);
  }
  if (states != nullptr) {
    for (size_t index = 0; index < states_length; ++index) {
      wlr_output_state_finish(&states[index].base);
    }
  }
  std::free(states);
  if (!accepted) {
    wlr_output_configuration_v1_send_failed(configuration);
    wlr_output_configuration_v1_destroy(configuration);
    return;
  }

  if (!test_only) {
    wlr_output_configuration_head_v1* head;
    wl_list_for_each(head, &configuration->heads, link) {
      wlr_output_layout_add(output_layout_, head->state.output, head->state.x,
                            head->state.y);
    }
    for (const std::unique_ptr<core::CompositorPrivate::Output>& output :
         compositor_->outputs_) {
      compositor_->ArrangeLayers(output.get());
    }
  }
  wlr_output_configuration_v1_send_succeeded(configuration);
  wlr_output_configuration_v1_destroy(configuration);
  if (!test_only) {
    UpdateOutputs();
  }
}

void ProtocolManager::ActivateConstraint(
    wlr_pointer_constraint_v1* constraint) {
  if (active_constraint_ == constraint) {
    return;
  }
  if (active_constraint_ != nullptr) {
    wlr_pointer_constraint_v1* previous = active_constraint_;
    active_constraint_ = nullptr;
    constraint_destroy_.Disconnect();
    if (previous->type == WLR_POINTER_CONSTRAINT_V1_LOCKED &&
        previous->current.cursor_hint.enabled &&
        seat_->pointer_state.focused_surface == previous->surface) {
      const double lx = cursor_->x + previous->current.cursor_hint.x -
                        seat_->pointer_state.sx;
      const double ly = cursor_->y + previous->current.cursor_hint.y -
                        seat_->pointer_state.sy;
      wlr_cursor_warp(cursor_, nullptr, lx, ly);
      wlr_seat_pointer_warp(seat_, previous->current.cursor_hint.x,
                            previous->current.cursor_hint.y);
    }
    wlr_pointer_constraint_v1_send_deactivated(previous);
  }
  active_constraint_ = constraint;
  if (constraint != nullptr) {
    constraint_destroy_.Connect(&constraint->events.destroy);
    wlr_pointer_constraint_v1_send_activated(constraint);
  }
}

void ProtocolManager::BeginSessionLock(wlr_session_lock_v1* lock) {
  if (session_lock_ != nullptr) {
    wlr_session_lock_v1_destroy(lock);
    return;
  }

  session_locked_ = true;
  compositor_->ResetCursorMode();
  ActivateConstraint(nullptr);
  if (wlr_seat_pointer_has_grab(seat_)) {
    wlr_seat_pointer_end_grab(seat_);
  }
  if (wlr_seat_keyboard_has_grab(seat_)) {
    wlr_seat_keyboard_end_grab(seat_);
  }
  if (wlr_seat_touch_has_grab(seat_)) {
    wlr_seat_touch_end_grab(seat_);
  }
  for (const std::unique_ptr<core::CompositorPrivate::TouchDevice>& device :
       compositor_->touch_devices_) {
    if (device->handle != nullptr) {
      compositor_->CancelTouchDevice(device->handle);
    }
  }
  wlr_seat_pointer_clear_focus(seat_);
  wlr_seat_keyboard_clear_focus(seat_);

  if (session_lock_blank_ == nullptr) {
    constexpr float color[4] = {0.0F, 0.0F, 0.0F, 1.0F};
    session_lock_blank_ =
        wlr_scene_rect_create(session_lock_parent_, 1, 1, color);
    if (session_lock_blank_ == nullptr) {
      ABSL_LOG(ERROR) << "Failed to create the secure session-lock blank";
      session_locked_ = false;
      wlr_session_lock_v1_destroy(lock);
      compositor_->FocusNextToplevel(nullptr);
      return;
    }
  }
  UpdateSessionLockGeometry();
  wlr_scene_node_raise_to_top(&session_lock_parent_->node);
  session_lock_ = std::make_unique<SessionLockState>(this, lock);
  wlr_scene_node_set_enabled(&session_lock_parent_->node, true);
  for (const std::unique_ptr<core::CompositorPrivate::Output>& output :
       compositor_->outputs_) {
    if (output->handle != nullptr && output->handle->enabled) {
      wlr_output_schedule_frame(output->handle);
    }
  }
  UpdateIdleInhibition();
}

void ProtocolManager::UnlockSession() {
  if (!session_locked_) {
    return;
  }
  session_locked_ = false;
  wlr_scene_node_set_enabled(&session_lock_parent_->node, false);
  compositor_->FocusNextToplevel(nullptr);
  UpdateIdleInhibition();
}

void ProtocolManager::FinishSessionLock(bool unlocked) {
  session_lock_.reset();
  if (!unlocked) {
    // If the locker crashes, retain the opaque blank and accept a replacement
    // locker. Revealing the desktop would turn a client crash into a lock
    // bypass.
    session_locked_ = true;
    wlr_scene_node_raise_to_top(&session_lock_parent_->node);
    wlr_scene_node_set_enabled(&session_lock_parent_->node, true);
    wlr_seat_pointer_clear_focus(seat_);
    wlr_seat_keyboard_clear_focus(seat_);
    ABSL_LOG(WARNING)
        << "Session-lock client disappeared; keeping the session locked";
  }
}

void ProtocolManager::FocusSessionLockSurface(wlr_surface* surface) {
  if (!session_locked_ || surface == nullptr || !surface->mapped) {
    return;
  }
  if (wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat_);
      keyboard != nullptr) {
    wlr_seat_set_keyboard(seat_, keyboard);
    wlr_seat_keyboard_notify_enter(seat_, surface, keyboard->keycodes,
                                   keyboard->num_keycodes,
                                   &keyboard->modifiers);
  }
}

void ProtocolManager::UpdateSessionLockGeometry() {
  if (session_lock_blank_ == nullptr || output_layout_ == nullptr) {
    return;
  }
  wlr_box box = {};
  wlr_output_layout_get_box(output_layout_, nullptr, &box);
  wlr_scene_node_set_position(&session_lock_blank_->node, box.x, box.y);
  wlr_scene_rect_set_size(session_lock_blank_, std::max(box.width, 1),
                          std::max(box.height, 1));
  if (session_lock_ != nullptr) {
    session_lock_->ConfigureSurfaces();
  }
}

void ProtocolManager::UpdateIdleInhibition() {
  bool inhibited = false;
  wlr_idle_inhibitor_v1* inhibitor;
  wl_list_for_each(inhibitor, &idle_inhibit_manager_->inhibitors, link) {
    wlr_surface* root = inhibitor->surface == nullptr
                            ? nullptr
                            : wlr_surface_get_root_surface(inhibitor->surface);
    if (root != nullptr && root->mapped &&
        (!session_locked_ ||
         wlr_session_lock_surface_v1_try_from_wlr_surface(root) != nullptr)) {
      inhibited = true;
      break;
    }
  }
  wlr_idle_notifier_v1_set_inhibited(idle_notifier_, inhibited);
}

ForeignToplevel* ProtocolManager::FindForeign(wlr_surface* surface) const {
  if (surface == nullptr) {
    return nullptr;
  }
  wlr_surface* root = wlr_surface_get_root_surface(surface);
  for (const std::unique_ptr<ForeignToplevel>& foreign : foreign_toplevels_) {
    if (wlr_surface_get_root_surface(foreign->Surface()) == root) {
      return foreign.get();
    }
  }
  return nullptr;
}

wlr_tablet_v2_tablet_tool* ProtocolManager::TabletTool(wlr_tablet_tool* tool) {
  const auto iterator = tablet_tools_.find(tool);
  if (iterator != tablet_tools_.end()) {
    return iterator->second;
  }
  wlr_tablet_v2_tablet_tool* protocol_tool =
      wlr_tablet_tool_create(tablet_manager_, seat_, tool);
  tablet_tools_[tool] = protocol_tool;
  return protocol_tool;
}

void ProtocolManager::TabletMotion(wlr_tablet_tool* tool, double, double) {
  double sx = 0;
  double sy = 0;
  wlr_surface* surface = nullptr;
  compositor_->ToplevelAt(cursor_->x, cursor_->y, &surface, &sx, &sy);
  wlr_tablet_v2_tablet_tool* protocol_tool = TabletTool(tool);
  if (protocol_tool != nullptr && protocol_tool->focused_surface == surface &&
      surface != nullptr) {
    wlr_tablet_v2_tablet_tool_notify_motion(protocol_tool, sx, sy);
  }
}

}  // namespace protocol
}  // namespace flakewm
