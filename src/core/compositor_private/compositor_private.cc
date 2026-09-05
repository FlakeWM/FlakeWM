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
 * This file is the implementation class of core compositor.
 */

#include "src/core/compositor_private/compositor_private.h"

#include <absl/log/absl_log.h>
#include <absl/strings/str_cat.h>
#include <linux/input-event-codes.h>
#include <sys/types.h>
#include <sysexits.h>
#include <time.h>
#include <unistd.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/utils/args_handler/args_handler.h"
#include "src/utils/misc/misc.h"
#include "src/xwayland/xwayland_manager/xwayland_manager.h"

namespace flakewm {
namespace core {
namespace {

constexpr std::size_t kMaximumClientBufferSize = 1024 * 1024;
constexpr uint32_t kXdgShellVersion = 5;
constexpr uint32_t kLayerShellVersion = 4;
constexpr uint32_t kDoubleClickIntervalMs = 400;
constexpr double kDoubleClickDistance = 6.0;
constexpr std::array<uint32_t, 4> kLayerOrder = {
    ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
    ZWLR_LAYER_SHELL_V1_LAYER_TOP,
    ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM,
    ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
};

const char* ResizeCursorName(uint32_t edges) {
  if ((edges & WLR_EDGE_TOP) && (edges & WLR_EDGE_LEFT)) {
    return "top_left_corner";
  }
  if ((edges & WLR_EDGE_TOP) && (edges & WLR_EDGE_RIGHT)) {
    return "top_right_corner";
  }
  if ((edges & WLR_EDGE_BOTTOM) && (edges & WLR_EDGE_LEFT)) {
    return "bottom_left_corner";
  }
  if ((edges & WLR_EDGE_BOTTOM) && (edges & WLR_EDGE_RIGHT)) {
    return "bottom_right_corner";
  }
  if (edges & WLR_EDGE_LEFT) {
    return "left_side";
  }
  if (edges & WLR_EDGE_RIGHT) {
    return "right_side";
  }
  if (edges & WLR_EDGE_TOP) {
    return "top_side";
  }
  return "bottom_side";
}

}  // namespace

CompositorPrivate::CompositorPrivate() {
  ABSL_LOG(INFO) << "FlakeWM compositor core is up!";
}

CompositorPrivate::~CompositorPrivate() { Destroy(); }

bool CompositorPrivate::Start(const utils::StartupArgs& startup_args) {
  // Reject repeated startup on the same compositor instance.
  if (started_) {
    ABSL_LOG(ERROR) << "Compositor is already started.";
    return false;
  }

  // Detect nested mode and prepare its Wlroots backend.
  nested_ = startup_args.nested || std::getenv("WAYLAND_DISPLAY") != nullptr ||
            std::getenv("WAYLAND_SOCKET") != nullptr ||
            std::getenv("DISPLAY") != nullptr;
  if (!ConfigureBackendEnvironment(startup_args)) {
    return false;
  }

  // Create the Wayland display first, everything else is attached to it.
  display_ = wl_display_create();
  if (display_ == nullptr) {
    return Fail("Failed to create Wayland display.");
  }

  wl_display_set_default_max_buffer_size(display_, kMaximumClientBufferSize);

  // Pick a backend from the current environment.
  backend_ =
      wlr_backend_autocreate(wl_display_get_event_loop(display_), nullptr);
  if (backend_ == nullptr) {
    return Fail("Failed to create Wlroots backend.");
  }

  // Prepare renderer and allocator for output buffers.
  renderer_ = wlr_renderer_autocreate(backend_);
  if (renderer_ == nullptr) {
    return Fail("Failed to create Wlroots renderer.");
  }

  if (!wlr_renderer_init_wl_display(renderer_, display_)) {
    return Fail("Failed to initialize renderer globals.");
  }

  allocator_ = wlr_allocator_autocreate(backend_, renderer_);
  if (allocator_ == nullptr) {
    return Fail("Failed to create wlroots allocator.");
  }

  // Publish basic globals required by regular Wayland clients.
  compositor_ = wlr_compositor_create(display_, 5, renderer_);
  if (compositor_ == nullptr) {
    return Fail("Failed to create wl_compositor global");
  }
  if (wlr_subcompositor_create(display_) == nullptr) {
    return Fail("Failed to create wl_subcompositor global");
  }
  if (wlr_data_device_manager_create(display_) == nullptr) {
    return Fail("Failed to create data-device manager");
  }
  if (wlr_primary_selection_v1_device_manager_create(display_) == nullptr) {
    return Fail("Failed to create primary-selection manager");
  }
  if (wlr_viewporter_create(display_) == nullptr) {
    return Fail("Failed to create viewporter global");
  }
  if (wlr_fractional_scale_manager_v1_create(display_, 1) == nullptr) {
    return Fail("Failed to create fractional-scale manager");
  }

  // Output layout is shared by scene rendering and output protocols.
  output_layout_ = wlr_output_layout_create(display_);
  if (output_layout_ == nullptr) {
    return Fail("Failed to create output layout");
  }
  if (wlr_xdg_output_manager_v1_create(display_, output_layout_) == nullptr) {
    return Fail("Failed to create xdg-output manager");
  }
  if (wlr_presentation_create(display_, backend_, 2) == nullptr) {
    return Fail("Failed to create presentation-time global");
  }

  // Create the root scene and attach it to the output layout.
  scene_ = wlr_scene_create();
  if (scene_ == nullptr) {
    return Fail("Failed to create scene graph");
  }

  // Keep regular windows between desktop and shell layers.
  shell_layer_trees_[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND] =
      wlr_scene_tree_create(&scene_->tree);
  shell_layer_trees_[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM] =
      wlr_scene_tree_create(&scene_->tree);
  toplevel_tree_ = wlr_scene_tree_create(&scene_->tree);
  shell_layer_trees_[ZWLR_LAYER_SHELL_V1_LAYER_TOP] =
      wlr_scene_tree_create(&scene_->tree);
  shell_layer_trees_[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY] =
      wlr_scene_tree_create(&scene_->tree);
  if (toplevel_tree_ == nullptr ||
      std::any_of(shell_layer_trees_.begin(), shell_layer_trees_.end(),
                  [](wlr_scene_tree* tree) { return tree == nullptr; })) {
    return Fail("Failed to create scene layers");
  }

  scene_layout_ = wlr_scene_attach_output_layout(scene_, output_layout_);
  if (scene_layout_ == nullptr) {
    return Fail("Failed to attach scene to output layout");
  }

  // XDG shell handles desktop windows, the seat handles client input.
  xdg_shell_ = wlr_xdg_shell_create(display_, kXdgShellVersion);
  if (xdg_shell_ == nullptr) {
    return Fail("Failed to create xdg-shell global");
  }
  xdg_decoration_manager_ = wlr_xdg_decoration_manager_v1_create(display_);
  if (xdg_decoration_manager_ == nullptr) {
    return Fail("Failed to create xdg-decoration manager");
  }
  layer_shell_ = wlr_layer_shell_v1_create(display_, kLayerShellVersion);
  if (layer_shell_ == nullptr) {
    return Fail("Failed to create layer-shell global");
  }
  seat_ = wlr_seat_create(display_, "seat0");
  if (seat_ == nullptr) {
    return Fail("Failed to create Wayland seat");
  }

  // Create one logical cursor for all pointer devices.
  cursor_ = wlr_cursor_create();
  cursor_manager_ = wlr_xcursor_manager_create(nullptr, 24);
  if (cursor_ == nullptr || cursor_manager_ == nullptr) {
    return Fail("Failed to create cursor");
  }
  wlr_cursor_attach_output_layout(cursor_, output_layout_);
  if (!wlr_xcursor_manager_load(cursor_manager_, 1.0F)) {
    ABSL_LOG(WARNING) << "Failed to load the default cursor theme";
  }
  wlr_cursor_set_xcursor(cursor_, cursor_manager_, "default");

  // Connect global object and input event listeners.
  new_output_.Connect(&backend_->events.new_output);
  new_toplevel_.Connect(&xdg_shell_->events.new_toplevel);
  new_popup_.Connect(&xdg_shell_->events.new_popup);
  new_xdg_decoration_.Connect(
      &xdg_decoration_manager_->events.new_toplevel_decoration);
  new_layer_surface_.Connect(&layer_shell_->events.new_surface);
  new_input_.Connect(&backend_->events.new_input);
  cursor_motion_.Connect(&cursor_->events.motion);
  cursor_motion_absolute_.Connect(&cursor_->events.motion_absolute);
  cursor_button_.Connect(&cursor_->events.button);
  cursor_axis_.Connect(&cursor_->events.axis);
  cursor_frame_.Connect(&cursor_->events.frame);
  request_cursor_.Connect(&seat_->events.request_set_cursor);
  pointer_focus_change_.Connect(&seat_->pointer_state.events.focus_change);
  request_selection_.Connect(&seat_->events.request_set_selection);
  request_primary_selection_.Connect(
      &seat_->events.request_set_primary_selection);
  wlr_seat_set_capabilities(seat_, WL_SEAT_CAPABILITY_POINTER);

  // Pick the first unused Wayland socket name.
  const std::optional<std::string> socket =
      utils::Misc::SelectWaylandSocket(display_);
  if (!socket.has_value()) {
    return Fail("Failed to create Wayland socket");
  }
  socket_name_ = *socket;

  // Globals must be ready before backend outputs start arriving.
  if (!wlr_backend_start(backend_)) {
    return Fail("Failed to start wlroots backend");
  }

  // Start XWayland, if enabled.
  if (!startup_args.disable_xwayland) {
    xwayland_ = std::make_unique<xwayland::XWaylandManager>(this);
    if (!xwayland_->Start(display_, compositor_)) {
      ABSL_LOG(ERROR) << "Failed to initialize XWayland, module skipped.";
      xwayland_.reset();
      unsetenv("DISPLAY");
    } else {
      setenv("DISPLAY", xwayland_->DisplayName(), 1);
      ABSL_LOG(INFO) << "XWayland listening on DISPLAY="
                     << xwayland_->DisplayName();
    }
  } else {
    // Never leak nested clients into the host X server when support is off.
    unsetenv("DISPLAY");
  }

  // Handle normal termination inside the Wayland event loop.
  wl_event_loop* event_loop = wl_display_get_event_loop(display_);
  signal_sources_[0] =
      wl_event_loop_add_signal(event_loop, SIGINT, OnTerminateSignal, this);
  signal_sources_[1] =
      wl_event_loop_add_signal(event_loop, SIGTERM, OnTerminateSignal, this);
  if (signal_sources_[0] == nullptr || signal_sources_[1] == nullptr) {
    return Fail("Failed to install compositor signal handlers");
  }
  qt_frame_timer_ = wl_event_loop_add_timer(event_loop, OnQtFrameTimer, this);
  if (qt_frame_timer_ == nullptr ||
      wl_event_source_timer_update(qt_frame_timer_, 16) < 0) {
    return Fail("Failed to install QtQuick frame timer");
  }

  // Export the child session environment after the socket is ready.
  setenv("WAYLAND_DISPLAY", socket_name_.c_str(), 1);
  unsetenv("WAYLAND_SOCKET");
  setenv("XDG_CURRENT_DESKTOP", "FlakeWM", 1);
  setenv("XDG_SESSION_DESKTOP", "FlakeWM", 1);
  setenv("XDG_SESSION_TYPE", "wayland", 1);

  // Start the user session only when one was requested.
  if (!startup_args.process.empty() && !Spawn(startup_args.process)) {
    return false;
  }

  started_ = true;
  ABSL_LOG(INFO) << "Running on WAYLAND_DISPLAY=" << socket_name_ << " ("
                 << (nested_ ? "nested" : "native") << ")";
  ABSL_LOG(INFO) << "XWayland "
                 << (xwayland_ != nullptr ? "enabled" : "disabled");
  return true;
}

void CompositorPrivate::Run() {
  // Run blocks until Stop() terminates the display.
  if (!started_ || display_ == nullptr) {
    ABSL_LOG(ERROR) << "Cannot run a compositor that has not started";
    return;
  }
  wl_display_run(display_);
}

void CompositorPrivate::Stop() {
  // Terminating an empty display is harmless during cleanup.
  if (display_ != nullptr) {
    wl_display_terminate(display_);
  }
}

CompositorPrivate::Output::Output(CompositorPrivate* compositor,
                                  wlr_output* output)
    : compositor(compositor),
      handle(output),
      frame(this, OnFrame),
      request_state(this, OnRequestState),
      destroy(this, OnDestroy) {}

bool CompositorPrivate::Output::CreateLayerTrees() {
  for (uint32_t layer = 0; layer < layer_trees.size(); ++layer) {
    layer_trees[layer] =
        wlr_scene_tree_create(compositor->shell_layer_trees_[layer]);
    if (layer_trees[layer] == nullptr) {
      DestroyLayerTrees();
      return false;
    }
  }
  return true;
}

wlr_scene_tree* CompositorPrivate::Output::LayerTree(uint32_t layer) const {
  if (layer >= layer_trees.size()) {
    return layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_TOP];
  }
  return layer_trees[layer];
}

void CompositorPrivate::Output::DestroyLayerTrees() {
  for (wlr_scene_tree*& tree : layer_trees) {
    if (tree != nullptr) {
      wlr_scene_node_destroy(&tree->node);
      tree = nullptr;
    }
  }
}

void CompositorPrivate::Output::OnFrame(Output* output, void*) {
  // Commit the scene for this output when a new frame is requested.
  if (output->scene_output == nullptr) {
    return;
  }
  if (!wlr_scene_output_commit(output->scene_output, nullptr)) {
    ABSL_LOG(ERROR) << "Failed to commit output frame";
    return;
  }

  // Tell clients when this frame was presented.
  timespec now = {};
  clock_gettime(CLOCK_MONOTONIC, &now);
  wlr_scene_output_send_frame_done(output->scene_output, &now);
}

void CompositorPrivate::Output::OnRequestState(
    Output* output, wlr_output_event_request_state* event) {
  // Backends may request their own output state changes.
  if (output->handle != nullptr &&
      !wlr_output_commit_state(output->handle, event->state)) {
    ABSL_LOG(ERROR) << "Backend-requested output state was rejected";
  } else {
    output->compositor->ArrangeLayers(output);
  }
}

void CompositorPrivate::Output::OnDestroy(Output* output, void*) {
  // Disconnect before Wlroots releases the output object.
  for (const std::unique_ptr<LayerSurface>& layer_surface :
       output->compositor->layer_surfaces_) {
    if (layer_surface->handle != nullptr &&
        layer_surface->handle->output == output->handle) {
      wlr_layer_surface_v1_destroy(layer_surface->handle);
    }
  }
  output->frame.Disconnect();
  output->request_state.Disconnect();
  output->destroy.Disconnect();
  output->DestroyLayerTrees();
  output->handle = nullptr;
  output->scene_output = nullptr;
}

CompositorPrivate::Keyboard::Keyboard(CompositorPrivate* compositor,
                                      wlr_input_device* device,
                                      wlr_keyboard* keyboard)
    : compositor(compositor),
      device(device),
      handle(keyboard),
      modifiers(this, OnModifiers),
      key(this, OnKey),
      destroy(this, OnDestroy) {}

void CompositorPrivate::Keyboard::OnModifiers(Keyboard* keyboard, void*) {
  // Forward the current modifier state through the active seat.
  if (keyboard->handle == nullptr) {
    return;
  }
  wlr_seat_set_keyboard(keyboard->compositor->seat_, keyboard->handle);
  wlr_seat_keyboard_notify_modifiers(keyboard->compositor->seat_,
                                     &keyboard->handle->modifiers);
}

void CompositorPrivate::Keyboard::OnKey(Keyboard* keyboard,
                                        wlr_keyboard_key_event* event) {
  // Forward key events from this keyboard to the focused client.
  if (keyboard->handle == nullptr) {
    return;
  }
  wlr_seat_set_keyboard(keyboard->compositor->seat_, keyboard->handle);
  wlr_seat_keyboard_notify_key(keyboard->compositor->seat_, event->time_msec,
                               event->keycode, event->state);
}

void CompositorPrivate::Keyboard::OnDestroy(Keyboard* keyboard, void*) {
  // Drop keyboard listeners and refresh seat capabilities.
  keyboard->modifiers.Disconnect();
  keyboard->key.Disconnect();
  keyboard->destroy.Disconnect();
  keyboard->device = nullptr;
  keyboard->handle = nullptr;
  keyboard->compositor->UpdateSeatCapabilities();
}

CompositorPrivate::XdgDecoration::XdgDecoration(
    CompositorPrivate* compositor, wlr_xdg_toplevel_decoration_v1* decoration)
    : compositor(compositor), handle(decoration) {}

void CompositorPrivate::XdgDecoration::ApplyMode() {
  if (handle == nullptr || handle->toplevel == nullptr ||
      handle->toplevel->base == nullptr) {
    return;
  }

  compositor->AttachSsd(compositor->FindToplevel(handle->toplevel));
  if (!handle->toplevel->base->initialized) {
    surface_commit.Connect(&handle->toplevel->base->surface->events.commit);
    return;
  }

  surface_commit.Disconnect();
  wlr_xdg_toplevel_decoration_v1_set_mode(
      handle, WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

void CompositorPrivate::XdgDecoration::OnRequestMode(XdgDecoration* decoration,
                                                     void*) {
  decoration->ApplyMode();
}

void CompositorPrivate::XdgDecoration::OnSurfaceCommit(
    XdgDecoration* decoration, void*) {
  if (decoration->handle != nullptr &&
      decoration->handle->toplevel != nullptr &&
      decoration->handle->toplevel->base->initialized) {
    decoration->ApplyMode();
  }
}

void CompositorPrivate::XdgDecoration::OnDestroy(XdgDecoration* decoration,
                                                 void*) {
  Toplevel* toplevel =
      decoration->handle == nullptr
          ? nullptr
          : decoration->compositor->FindToplevel(decoration->handle->toplevel);
  if (toplevel != nullptr) {
    toplevel->ssd.reset();
    toplevel->ssd_clip.reset();
    toplevel->ssd_initial_position_pending = false;
  }
  decoration->request_mode.Disconnect();
  decoration->surface_commit.Disconnect();
  decoration->destroy.Disconnect();
  if (decoration->handle != nullptr) {
    decoration->handle->data = nullptr;
    decoration->handle = nullptr;
  }
}

CompositorPrivate::Toplevel::Toplevel(CompositorPrivate* compositor,
                                      wlr_xdg_toplevel* toplevel)
    : compositor(compositor), handle(toplevel) {}

CompositorPrivate::Toplevel::Toplevel(CompositorPrivate* compositor)
    : compositor(compositor) {}

CompositorPrivate::Toplevel::~Toplevel() = default;

bool CompositorPrivate::Toplevel::IsAlive() const { return handle != nullptr; }

bool CompositorPrivate::Toplevel::IsXWayland() const { return false; }

bool CompositorPrivate::Toplevel::WantsFocus() const { return true; }

bool CompositorPrivate::Toplevel::CanManage() const { return true; }

bool CompositorPrivate::Toplevel::CanMinimize() const {
  return handle != nullptr;
}

bool CompositorPrivate::Toplevel::CanMaximize() const {
  if (handle == nullptr || handle->parent != nullptr) {
    return false;
  }
  const wlr_xdg_toplevel_state& state = handle->current;
  const bool fixed_width = state.min_width > 0 && state.max_width > 0 &&
                           state.min_width == state.max_width;
  const bool fixed_height = state.min_height > 0 && state.max_height > 0 &&
                            state.min_height == state.max_height;
  return !fixed_width && !fixed_height;
}

bool CompositorPrivate::Toplevel::RequestedMaximized() const {
  return handle != nullptr && handle->requested.maximized;
}

bool CompositorPrivate::Toplevel::RequestedFullscreen() const {
  return handle != nullptr && handle->requested.fullscreen;
}

wlr_surface* CompositorPrivate::Toplevel::Surface() const {
  return handle == nullptr ? nullptr : handle->base->surface;
}

wlr_box CompositorPrivate::Toplevel::Geometry() const {
  return handle == nullptr ? wlr_box{} : handle->base->geometry;
}

void CompositorPrivate::Toplevel::Configure(const wlr_box& box) const {
  if (scene_tree == nullptr || box.width <= 0 || box.height <= 0) {
    return;
  }

  const wlr_box geometry = Geometry();
  const wlr_box content = ssd == nullptr ? box : ssd->ContentGeometry(box);
  wlr_scene_node_set_position(&scene_tree->node, content.x - geometry.x,
                              content.y - geometry.y);
  if (handle != nullptr) {
    wlr_xdg_toplevel_set_size(handle, content.width, content.height);
  }
}

void CompositorPrivate::Toplevel::SetActivated(bool activated) const {
  if (handle != nullptr) {
    wlr_xdg_toplevel_set_activated(handle, activated);
  }
}

void CompositorPrivate::Toplevel::SetMaximizedState(bool maximized) const {
  if (handle != nullptr) {
    wlr_xdg_toplevel_set_maximized(handle, maximized);
  }
}

void CompositorPrivate::Toplevel::SetMinimizedState(bool) const {}

void CompositorPrivate::Toplevel::SetFullscreenState(bool fullscreen) const {
  if (handle != nullptr) {
    wlr_xdg_toplevel_set_fullscreen(handle, fullscreen);
  }
}

void CompositorPrivate::Toplevel::Restack() const {}

wlr_box CompositorPrivate::Toplevel::FrameGeometry() const {
  const wlr_box geometry = Geometry();
  return ssd == nullptr ? geometry : ssd->FrameGeometry(geometry);
}

void CompositorPrivate::Toplevel::UpdateCapabilities() {
  uint32_t capabilities = WLR_XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN;
  if (CanMaximize()) {
    capabilities |= WLR_XDG_TOPLEVEL_WM_CAPABILITIES_MAXIMIZE;
  }
  if (CanMinimize()) {
    capabilities |= WLR_XDG_TOPLEVEL_WM_CAPABILITIES_MINIMIZE;
  }

  if (ssd != nullptr) {
    ssd->SetCapabilities(CanMinimize(), CanMaximize());
  }
  if (handle == nullptr || handle->base == nullptr ||
      !handle->base->initialized ||
      wl_resource_get_version(handle->resource) < kXdgShellVersion ||
      (capabilities_advertised && advertised_capabilities == capabilities)) {
    return;
  }
  wlr_xdg_toplevel_set_wm_capabilities(handle, capabilities);
  advertised_capabilities = capabilities;
  capabilities_advertised = true;
}

void CompositorPrivate::Toplevel::OnMap(Toplevel* toplevel, void*) {
  // Honor the initial state request before focusing the new window.
  toplevel->mapped = true;
  wlr_scene_node_set_enabled(&toplevel->scene_tree->node, true);
  if (toplevel->RequestedMaximized() && toplevel->CanManage()) {
    toplevel->compositor->SetMaximized(toplevel, true);
  }
  if (toplevel->WantsFocus()) {
    toplevel->compositor->FocusToplevel(toplevel);
  }
}

void CompositorPrivate::Toplevel::OnUnmap(Toplevel* toplevel, void*) {
  // Stop any grab which still belongs to this window.
  const bool had_focus =
      toplevel->Surface() != nullptr &&
      toplevel->compositor->seat_->keyboard_state.focused_surface ==
          toplevel->Surface();
  toplevel->mapped = false;
  if (toplevel->compositor->grabbed_toplevel_ == toplevel) {
    toplevel->compositor->ResetCursorMode();
  }

  if (had_focus) {
    toplevel->compositor->FocusNextToplevel(toplevel);
  }
}

void CompositorPrivate::Toplevel::OnCommit(Toplevel* toplevel, void*) {
  if (toplevel->handle == nullptr) {
    return;
  }

  toplevel->UpdateCapabilities();

  // The initial configure leaves sizing to the client.
  if (toplevel->handle->base->initial_commit) {
    wlr_xdg_toplevel_set_size(toplevel->handle, 0, 0);
  }

  const wlr_box geometry = toplevel->Geometry();
  if (toplevel->ssd != nullptr) {
    toplevel->ssd->SetGeometry(geometry);
    toplevel->ssd->SetTitle(toplevel->handle->title == nullptr
                                ? std::string{}
                                : toplevel->handle->title);
    toplevel->ssd->SetAppId(toplevel->handle->app_id == nullptr
                                ? std::string{}
                                : toplevel->handle->app_id);
    if (toplevel->ssd_clip != nullptr) {
      toplevel->ssd_clip->Update(geometry, toplevel->maximized);
    }
    if (toplevel->ssd_initial_position_pending && geometry.width > 0 &&
        geometry.height > 0) {
      const wlr_box frame = toplevel->FrameGeometry();
      if (toplevel->scene_tree->node.x == 0 &&
          toplevel->scene_tree->node.y == 0) {
        wlr_scene_node_set_position(&toplevel->scene_tree->node, -frame.x,
                                    -frame.y);
      }
      toplevel->ssd_initial_position_pending = false;
    }
  }

  // Geometry can change after configure. Keep the requested frame position.
  const wlr_box frame = toplevel->FrameGeometry();
  if (toplevel->maximized && toplevel->maximized_box.width > 0 &&
      toplevel->maximized_box.height > 0) {
    wlr_scene_node_set_position(&toplevel->scene_tree->node,
                                toplevel->maximized_box.x - frame.x,
                                toplevel->maximized_box.y - frame.y);
  } else if (toplevel->restore_position_pending && toplevel->has_restore_box) {
    wlr_scene_node_set_position(&toplevel->scene_tree->node,
                                toplevel->restore_box.x - frame.x,
                                toplevel->restore_box.y - frame.y);
    toplevel->restore_position_pending = false;
  }
}

void CompositorPrivate::Toplevel::OnRequestMaximize(Toplevel* toplevel, void*) {
  // Apply the state requested by the client.
  if (toplevel->CanManage()) {
    toplevel->compositor->SetMaximized(
        toplevel, toplevel->CanMaximize() && toplevel->RequestedMaximized());
  }
}

void CompositorPrivate::Toplevel::OnRequestMinimize(Toplevel* toplevel, void*) {
  // Minimize hides the scene node until another shell component restores it.
  if (toplevel->CanMinimize()) {
    toplevel->compositor->Minimize(toplevel);
  }
}

void CompositorPrivate::Toplevel::OnRequestFullscreen(Toplevel* toplevel,
                                                      void*) {
  toplevel->SetFullscreenState(toplevel->RequestedFullscreen());
}

void CompositorPrivate::Toplevel::OnRequestMove(Toplevel* toplevel, void*) {
  // XDG move requests start a compositor-side pointer grab.
  toplevel->compositor->BeginInteractive(toplevel, CursorMode::kMove, 0);
}

void CompositorPrivate::Toplevel::OnRequestResize(
    Toplevel* toplevel, wlr_xdg_toplevel_resize_event* event) {
  // Keep the requested edges for the following pointer motion.
  toplevel->compositor->BeginInteractive(toplevel, CursorMode::kResize,
                                         event->edges);
}

void CompositorPrivate::Toplevel::OnSetTitle(Toplevel* toplevel, void*) {
  if (toplevel->ssd != nullptr && toplevel->handle != nullptr) {
    toplevel->ssd->SetTitle(toplevel->handle->title == nullptr
                                ? std::string{}
                                : toplevel->handle->title);
  }
}

void CompositorPrivate::Toplevel::OnSetAppId(Toplevel* toplevel, void*) {
  if (toplevel->ssd != nullptr && toplevel->handle != nullptr) {
    toplevel->ssd->SetAppId(toplevel->handle->app_id == nullptr
                                ? std::string{}
                                : toplevel->handle->app_id);
  }
}

void CompositorPrivate::Toplevel::OnSetParent(Toplevel* toplevel, void*) {
  toplevel->UpdateCapabilities();
  if (toplevel->ssd != nullptr && toplevel->handle != nullptr) {
    toplevel->ssd->SetDialog(toplevel->handle->parent != nullptr);
  }
  if (toplevel->maximized && !toplevel->CanMaximize()) {
    toplevel->compositor->SetMaximized(toplevel, false);
  }
}

void CompositorPrivate::Toplevel::OnDestroy(Toplevel* toplevel, void*) {
  // End its active grab before disconnecting protocol listeners.
  if (toplevel->compositor->grabbed_toplevel_ == toplevel) {
    toplevel->compositor->ResetCursorMode();
  }
  toplevel->map.Disconnect();
  toplevel->unmap.Disconnect();
  toplevel->commit.Disconnect();
  toplevel->destroy.Disconnect();
  toplevel->request_move.Disconnect();
  toplevel->request_resize.Disconnect();
  toplevel->request_maximize.Disconnect();
  toplevel->request_minimize.Disconnect();
  toplevel->request_fullscreen.Disconnect();
  toplevel->set_title.Disconnect();
  toplevel->set_app_id.Disconnect();
  toplevel->set_parent.Disconnect();
  toplevel->ssd.reset();
  toplevel->ssd_clip.reset();
  toplevel->handle = nullptr;
  toplevel->scene_tree = nullptr;
  toplevel->mapped = false;
}

CompositorPrivate::Popup::Popup(CompositorPrivate* compositor,
                                wlr_xdg_popup* popup)
    : compositor(compositor),
      handle(popup),
      commit(this, OnCommit),
      destroy(this, OnDestroy) {}

void CompositorPrivate::Popup::OnCommit(Popup* popup, void*) {
  // Initial configure lets the popup client submit its first buffer.
  if (popup->handle != nullptr && popup->handle->base->initial_commit) {
    wlr_xdg_surface_schedule_configure(popup->handle->base);
  }
}

void CompositorPrivate::Popup::OnDestroy(Popup* popup, void*) {
  // The scene node is owned by its parent tree.
  popup->commit.Disconnect();
  popup->destroy.Disconnect();
  popup->handle = nullptr;
  popup->scene_tree = nullptr;
}

void CompositorPrivate::UpdateSeatCapabilities() {
  // Pointer is always available, keyboard depends on attached devices.
  uint32_t capabilities = WL_SEAT_CAPABILITY_POINTER;
  for (const std::unique_ptr<Keyboard>& keyboard : keyboards_) {
    if (keyboard->handle != nullptr) {
      capabilities |= WL_SEAT_CAPABILITY_KEYBOARD;
      break;
    }
  }
  wlr_seat_set_capabilities(seat_, capabilities);
}

void CompositorPrivate::AddKeyboard(wlr_input_device* device) {
  wlr_keyboard* handle = wlr_keyboard_from_input_device(device);
  xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  xkb_keymap* keymap = context == nullptr
                           ? nullptr
                           : xkb_keymap_new_from_names(
                                 context, nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS);
  if (context == nullptr || keymap == nullptr) {
    ABSL_LOG(ERROR) << "Failed to create the default keyboard keymap";
    if (keymap != nullptr) {
      xkb_keymap_unref(keymap);
    }
    if (context != nullptr) {
      xkb_context_unref(context);
    }
    return;
  }

  // Wlroots keeps its own reference after setting the keymap.
  wlr_keyboard_set_keymap(handle, keymap);
  xkb_keymap_unref(keymap);
  xkb_context_unref(context);
  wlr_keyboard_set_repeat_info(handle, 25, 600);

  // Connect the device and make it the active seat keyboard.
  auto keyboard = std::make_unique<Keyboard>(this, device, handle);
  keyboard->modifiers.Connect(&handle->events.modifiers);
  keyboard->key.Connect(&handle->events.key);
  keyboard->destroy.Connect(&device->events.destroy);
  wlr_seat_set_keyboard(seat_, handle);
  keyboards_.push_back(std::move(keyboard));
}

void CompositorPrivate::OnNewInput(CompositorPrivate* compositor,
                                   wlr_input_device* device) {
  // Attach supported devices to the shared seat and cursor.
  switch (device->type) {
    case WLR_INPUT_DEVICE_KEYBOARD:
      compositor->AddKeyboard(device);
      break;
    case WLR_INPUT_DEVICE_POINTER:
      wlr_cursor_attach_input_device(compositor->cursor_, device);
      break;
    default:
      ABSL_LOG(INFO) << "Ignoring unsupported input device " << device->name;
      break;
  }
  compositor->UpdateSeatCapabilities();
}

CompositorPrivate::Toplevel* CompositorPrivate::ToplevelAt(
    double layout_x, double layout_y, wlr_surface** surface, double* surface_x,
    double* surface_y) const {
  // Hit test the scene and return both the surface and owning toplevel.
  *surface = nullptr;
  wlr_scene_node* node = wlr_scene_node_at(&scene_->tree.node, layout_x,
                                           layout_y, surface_x, surface_y);
  if (node == nullptr) {
    return nullptr;
  }

  // Decoration buffers belong to a toplevel but never receive client focus.
  if (node->type == WLR_SCENE_NODE_BUFFER) {
    wlr_scene_surface* scene_surface =
        wlr_scene_surface_try_from_buffer(wlr_scene_buffer_from_node(node));
    if (scene_surface != nullptr) {
      *surface = scene_surface->surface;
    }
  }

  // Walk up until reaching the toplevel tree marked in OnNewToplevel().
  wlr_scene_tree* tree = node->parent;
  while (tree != nullptr && tree->node.data == nullptr) {
    tree = tree->node.parent;
  }
  return tree == nullptr ? nullptr : static_cast<Toplevel*>(tree->node.data);
}

CompositorPrivate::Toplevel* CompositorPrivate::FindToplevel(
    wlr_xdg_toplevel* handle) const {
  for (const std::unique_ptr<Toplevel>& toplevel : toplevels_) {
    if (toplevel->handle == handle) {
      return toplevel.get();
    }
  }
  return nullptr;
}

void CompositorPrivate::AttachSsd(Toplevel* toplevel) {
  if (toplevel == nullptr || toplevel->scene_tree == nullptr ||
      toplevel->ssd != nullptr) {
    return;
  }

  auto clip =
      view::SsdSurfaceClip::Create(toplevel->scene_tree, toplevel->Surface());
  toplevel->ssd = view::Ssd::Create(toplevel->scene_tree);
  if (toplevel->ssd == nullptr) {
    ABSL_LOG(ERROR) << "Failed to create server-side decoration";
    return;
  }
  toplevel->ssd->SetMaximized(toplevel->maximized);
  toplevel->ssd->SetDialog(toplevel->handle->parent != nullptr);
  toplevel->ssd->SetCapabilities(toplevel->CanMinimize(),
                                 toplevel->CanMaximize());
  toplevel->ssd->SetGeometry(toplevel->Geometry());
  toplevel->ssd->SetTitle(toplevel->handle->title == nullptr
                              ? std::string{}
                              : toplevel->handle->title);
  toplevel->ssd->SetAppId(toplevel->handle->app_id == nullptr
                              ? std::string{}
                              : toplevel->handle->app_id);
  toplevel->ssd_clip = std::move(clip);
  if (toplevel->ssd_clip == nullptr) {
    ABSL_LOG(ERROR) << "Failed to create SSD surface clip";
  } else {
    toplevel->ssd_clip->Update(toplevel->Geometry(), toplevel->maximized);
  }
  toplevel->ssd_initial_position_pending = true;
}

view::Ssd::HitTarget CompositorPrivate::SsdHitAt(
    const Toplevel* toplevel) const {
  if (toplevel == nullptr || toplevel->ssd == nullptr ||
      toplevel->scene_tree == nullptr) {
    return {};
  }
  return toplevel->ssd->HitTest(cursor_->x - toplevel->scene_tree->node.x,
                                cursor_->y - toplevel->scene_tree->node.y);
}

void CompositorPrivate::FocusToplevel(Toplevel* toplevel) {
  // Ignore windows which cannot receive focus.
  if (toplevel == nullptr || !toplevel->mapped || !toplevel->IsAlive() ||
      toplevel->Surface() == nullptr || !toplevel->WantsFocus()) {
    return;
  }

  // Exclusive shell layers keep the keyboard until they disappear.
  for (auto iterator = layer_surfaces_.rbegin();
       iterator != layer_surfaces_.rend(); ++iterator) {
    LayerSurface* layer_surface = iterator->get();
    if (layer_surface->mapped && layer_surface->handle != nullptr &&
        layer_surface->handle->current.keyboard_interactive ==
            ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE) {
      FocusLayerSurface(layer_surface);
      return;
    }
  }

  for (const std::unique_ptr<Toplevel>& candidate : toplevels_) {
    if (candidate->ssd != nullptr) {
      candidate->ssd->SetActive(candidate.get() == toplevel);
    }
  }

  wlr_surface* surface = toplevel->Surface();
  wlr_surface* previous_surface = seat_->keyboard_state.focused_surface;
  if (previous_surface == surface) {
    return;
  }
  // Deactivate the previously focused XDG toplevel.
  if (previous_surface != nullptr) {
    wlr_xdg_toplevel* previous =
        wlr_xdg_toplevel_try_from_wlr_surface(previous_surface);
    if (previous != nullptr) {
      wlr_xdg_toplevel_set_activated(previous, false);
    }

    wlr_xwayland_surface* previous_xwayland =
        wlr_xwayland_surface_try_from_wlr_surface(previous_surface);
    if (previous_xwayland != nullptr) {
      wlr_xwayland_surface_activate(previous_xwayland, false);
    }
  }

  // Raise, activate and send the current keyboard state.
  wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
  toplevel->Restack();
  toplevel->SetActivated(true);
  if (wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat_);
      keyboard != nullptr) {
    wlr_seat_keyboard_notify_enter(seat_, surface, keyboard->keycodes,
                                   keyboard->num_keycodes,
                                   &keyboard->modifiers);
  }
}

void CompositorPrivate::FocusNextToplevel(Toplevel* excluding) {
  // Exclusive shell layers have priority over regular windows.
  for (auto iterator = layer_surfaces_.rbegin();
       iterator != layer_surfaces_.rend(); ++iterator) {
    LayerSurface* layer_surface = iterator->get();
    if (layer_surface->mapped && layer_surface->handle != nullptr &&
        layer_surface->handle->current.keyboard_interactive ==
            ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE) {
      FocusLayerSurface(layer_surface);
      return;
    }
  }

  // Pick the topmost mapped window other than the one being removed.
  for (auto iterator = toplevels_.rbegin(); iterator != toplevels_.rend();
       ++iterator) {
    Toplevel* candidate = iterator->get();
    if (candidate != excluding && candidate->mapped && !candidate->minimized &&
        candidate->IsAlive()) {
      FocusToplevel(candidate);
      return;
    }
  }
  // No usable window remains.
  wlr_seat_keyboard_clear_focus(seat_);
}

void CompositorPrivate::FocusLayerSurface(LayerSurface* layer_surface) {
  if (layer_surface == nullptr || !layer_surface->mapped ||
      layer_surface->handle == nullptr ||
      layer_surface->handle->current.keyboard_interactive ==
          ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE) {
    return;
  }

  for (const std::unique_ptr<Toplevel>& toplevel : toplevels_) {
    if (toplevel->ssd != nullptr) {
      toplevel->ssd->SetActive(false);
    }
  }

  wlr_surface* surface = layer_surface->handle->surface;
  wlr_surface* previous_surface = seat_->keyboard_state.focused_surface;
  if (previous_surface == surface) {
    return;
  }
  if (previous_surface != nullptr) {
    wlr_xdg_toplevel* previous =
        wlr_xdg_toplevel_try_from_wlr_surface(previous_surface);
    if (previous != nullptr) {
      wlr_xdg_toplevel_set_activated(previous, false);
    }

    wlr_xwayland_surface* previous_xwayland =
        wlr_xwayland_surface_try_from_wlr_surface(previous_surface);
    if (previous_xwayland != nullptr) {
      wlr_xwayland_surface_activate(previous_xwayland, false);
    }
  }

  if (wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat_);
      keyboard != nullptr) {
    wlr_seat_keyboard_notify_enter(seat_, surface, keyboard->keycodes,
                                   keyboard->num_keycodes,
                                   &keyboard->modifiers);
  }
}

LayerSurface* CompositorPrivate::LayerSurfaceFor(wlr_surface* surface) const {
  if (surface == nullptr) {
    return nullptr;
  }

  wlr_surface* root = wlr_surface_get_root_surface(surface);
  for (const std::unique_ptr<LayerSurface>& layer_surface : layer_surfaces_) {
    if (layer_surface->handle != nullptr &&
        layer_surface->handle->surface == root) {
      return layer_surface.get();
    }
  }
  return nullptr;
}

CompositorPrivate::Output* CompositorPrivate::FindOutput(
    wlr_output* output) const {
  for (const std::unique_ptr<Output>& state : outputs_) {
    if (state->handle == output) {
      return state.get();
    }
  }
  return nullptr;
}

wlr_box CompositorPrivate::OutputBoxAt(double layout_x, double layout_y) const {
  // Fall back to the center output when the point is outside the layout.
  wlr_output* output =
      wlr_output_layout_output_at(output_layout_, layout_x, layout_y);
  if (output == nullptr) {
    output = wlr_output_layout_get_center_output(output_layout_);
  }
  wlr_box box = {};
  wlr_output_layout_get_box(output_layout_, output, &box);
  return box;
}

wlr_box CompositorPrivate::UsableOutputBox(wlr_output* output) const {
  Output* state = FindOutput(output);
  if (state != nullptr && state->usable_box.width > 0 &&
      state->usable_box.height > 0) {
    return state->usable_box;
  }

  wlr_box box = {};
  wlr_output_layout_get_box(output_layout_, output, &box);
  return box;
}

void CompositorPrivate::ArrangeLayers(Output* output) {
  if (output == nullptr || output->handle == nullptr) {
    return;
  }

  wlr_box output_box = {};
  wlr_output_layout_get_box(output_layout_, output->handle, &output_box);
  wlr_box full_box = {};
  wlr_output_effective_resolution(output->handle, &full_box.width,
                                  &full_box.height);
  if (full_box.width <= 0 || full_box.height <= 0) {
    return;
  }

  // Layer coordinates are output-local; their parent trees carry the output
  // layout offset.
  for (wlr_scene_tree* tree : output->layer_trees) {
    wlr_scene_node_set_position(&tree->node, output_box.x, output_box.y);
  }

  // Exclusive surfaces reserve space before the remaining layers are placed.
  wlr_box usable_box = full_box;
  for (const bool exclusive : {true, false}) {
    for (const uint32_t layer : kLayerOrder) {
      wlr_scene_tree* tree = output->LayerTree(layer);
      for (const std::unique_ptr<LayerSurface>& state : layer_surfaces_) {
        if (state->handle == nullptr || !state->handle->initialized ||
            state->handle->output != output->handle ||
            state->handle->current.layer != layer ||
            state->scene_surface->tree->node.parent != tree ||
            (state->handle->current.exclusive_zone > 0) != exclusive) {
          continue;
        }
        wlr_scene_layer_surface_v1_configure(state->scene_surface, &full_box,
                                             &usable_box);
      }
    }
  }
  output->usable_box = usable_box;
  output->usable_box.x += output_box.x;
  output->usable_box.y += output_box.y;

  // Maximized windows follow changes made by panels and other exclusive layers.
  for (const std::unique_ptr<Toplevel>& toplevel : toplevels_) {
    if (!toplevel->maximized || !toplevel->IsAlive() ||
        toplevel->maximized_output != output->handle) {
      continue;
    }
    toplevel->maximized_box = output->usable_box;
    toplevel->Configure(output->usable_box);
  }
}

void CompositorPrivate::SetMaximized(Toplevel* toplevel, bool maximized) {
  // State changes need both a live protocol handle and a scene node.
  if (toplevel == nullptr || !toplevel->IsAlive() ||
      toplevel->scene_tree == nullptr) {
    return;
  }

  // Duplicate requests still need their client-visible state refreshed.
  if (maximized == toplevel->maximized) {
    if (toplevel->ssd != nullptr) {
      toplevel->ssd->SetMaximized(maximized);
    }
    toplevel->SetMaximizedState(maximized);
    return;
  }

  const wlr_box frame = toplevel->FrameGeometry();
  if (maximized) {
    // Save the complete frame, including the server-side decoration.
    if (frame.width > 0 && frame.height > 0) {
      toplevel->restore_box = {
          .x = toplevel->scene_tree->node.x + frame.x,
          .y = toplevel->scene_tree->node.y + frame.y,
          .width = frame.width,
          .height = frame.height,
      };
      toplevel->has_restore_box = true;
    }
  }

  toplevel->maximized = maximized;
  if (toplevel->ssd != nullptr) {
    toplevel->ssd->SetMaximized(maximized);
  }
  if (toplevel->ssd_clip != nullptr) {
    toplevel->ssd_clip->Update(toplevel->Geometry(), maximized);
  }

  if (maximized) {
    // Maximize onto the usable area of the output under the pointer.
    toplevel->maximized_output =
        wlr_output_layout_output_at(output_layout_, cursor_->x, cursor_->y);
    if (toplevel->maximized_output == nullptr) {
      toplevel->maximized_output =
          wlr_output_layout_get_center_output(output_layout_);
    }
    toplevel->maximized_box = UsableOutputBox(toplevel->maximized_output);
    if (toplevel->maximized_box.width > 0 &&
        toplevel->maximized_box.height > 0) {
      toplevel->Configure(toplevel->maximized_box);
    }
    // A maximize request also brings a hidden window back.
    if (toplevel->minimized) {
      wlr_scene_node_set_enabled(&toplevel->scene_tree->node, true);
      toplevel->minimized = false;
    }
  } else if (toplevel->has_restore_box) {
    // Restore now, then correct CSD offsets on the next commit.
    toplevel->Configure(toplevel->restore_box);
    toplevel->restore_position_pending = true;
  }

  if (!maximized) {
    toplevel->maximized_output = nullptr;
  }
  toplevel->SetMaximizedState(maximized);
}

void CompositorPrivate::ToggleMaximized(Toplevel* toplevel) {
  // Used by titlebar double click.
  if (toplevel != nullptr && toplevel->CanMaximize()) {
    SetMaximized(toplevel, !toplevel->maximized);
  }
}

void CompositorPrivate::Minimize(Toplevel* toplevel) {
  // A minimized window stays mapped but is removed from the scene.
  if (toplevel == nullptr || !toplevel->CanMinimize() || !toplevel->mapped ||
      toplevel->minimized || toplevel->scene_tree == nullptr) {
    return;
  }
  toplevel->minimized = true;
  toplevel->SetMinimizedState(true);
  wlr_scene_node_set_enabled(&toplevel->scene_tree->node, false);
  // Move keyboard focus away if this was the active window.
  if (seat_->keyboard_state.focused_surface == toplevel->Surface()) {
    FocusNextToplevel(toplevel);
  }
  wlr_seat_pointer_clear_focus(seat_);
}

void CompositorPrivate::RestoreForMove(Toplevel* toplevel) {
  // Dragging a maximized window begins from its previous normal size.
  if (toplevel == nullptr || !toplevel->maximized ||
      !toplevel->has_restore_box) {
    return;
  }

  // Keep the same horizontal point of the titlebar under the pointer.
  const wlr_box output_box = OutputBoxAt(cursor_->x, cursor_->y);
  const double horizontal_ratio =
      output_box.width > 0
          ? std::clamp((cursor_->x - output_box.x) / output_box.width, 0.0, 1.0)
          : 0.5;
  const wlr_box restore_box = toplevel->restore_box;
  const wlr_box frame = toplevel->FrameGeometry();
  const double maximum_titlebar_offset =
      toplevel->ssd == nullptr ? 48.0 : toplevel->ssd->TitlebarHeight();
  const double titlebar_offset =
      std::clamp(cursor_->y - (toplevel->scene_tree->node.y + frame.y), 0.0,
                 maximum_titlebar_offset);

  // Save the new restore position for the coming normal-state commit.
  SetMaximized(toplevel, false);
  const int frame_x = cursor_->x - restore_box.width * horizontal_ratio;
  const int frame_y = cursor_->y - titlebar_offset;
  toplevel->restore_box.x = frame_x;
  toplevel->restore_box.y = frame_y;
  const wlr_box restored_frame = toplevel->FrameGeometry();
  wlr_scene_node_set_position(&toplevel->scene_tree->node,
                              frame_x - restored_frame.x,
                              frame_y - restored_frame.y);
}

bool CompositorPrivate::CursorAtOutputTop() const {
  // Four pixels makes the top edge easier to hit during a drag.
  const wlr_box output_box = OutputBoxAt(cursor_->x, cursor_->y);
  return output_box.width > 0 && cursor_->x >= output_box.x &&
         cursor_->x < output_box.x + output_box.width &&
         cursor_->y <= output_box.y + 4;
}

bool CompositorPrivate::IsTitlebarPoint(const Toplevel* toplevel,
                                        wlr_surface* surface,
                                        double surface_y) const {
  // Only the main surface owns the client-side titlebar.
  if (toplevel == nullptr || !toplevel->IsAlive() || toplevel->ssd != nullptr ||
      surface != toplevel->Surface()) {
    return false;
  }
  // GTK and common CSD titlebars fit inside the first 48 pixels.
  const int geometry_y = toplevel->Geometry().y;
  return surface_y >= geometry_y && surface_y < geometry_y + 48;
}

void CompositorPrivate::ResetCursorMode() {
  // Drop all state left by a move or resize grab.
  cursor_mode_ = CursorMode::kPassthrough;
  grabbed_toplevel_ = nullptr;
  maximize_on_release_ = false;
}

void CompositorPrivate::EndInteractive() {
  // A move ending at the top edge becomes maximized.
  Toplevel* toplevel = grabbed_toplevel_;
  const bool should_maximize =
      cursor_mode_ == CursorMode::kMove && maximize_on_release_;
  ResetCursorMode();
  if (should_maximize) {
    SetMaximized(toplevel, true);
  }
}

void CompositorPrivate::BeginInteractive(Toplevel* toplevel, CursorMode mode,
                                         uint32_t edges) {
  // Unmapped windows cannot begin pointer grabs.
  if (toplevel == nullptr || !toplevel->mapped ||
      toplevel->scene_tree == nullptr) {
    return;
  }

  // Normalize a maximized window before it starts moving.
  if (mode == CursorMode::kMove && toplevel->maximized) {
    RestoreForMove(toplevel);
  }

  // Remember the pointer offset so the window does not jump.
  grabbed_toplevel_ = toplevel;
  cursor_mode_ = mode;
  resize_edges_ = edges;
  if (mode == CursorMode::kMove) {
    grab_x_ = cursor_->x - toplevel->scene_tree->node.x;
    grab_y_ = cursor_->y - toplevel->scene_tree->node.y;
    return;
  }

  // Resize grabs use global geometry and the requested border.
  grab_box_ = toplevel->FrameGeometry();
  grab_box_.x += toplevel->scene_tree->node.x;
  grab_box_.y += toplevel->scene_tree->node.y;
  const double border_x =
      grab_box_.x + ((edges & WLR_EDGE_RIGHT) ? grab_box_.width : 0);
  const double border_y =
      grab_box_.y + ((edges & WLR_EDGE_BOTTOM) ? grab_box_.height : 0);
  grab_x_ = cursor_->x - border_x;
  grab_y_ = cursor_->y - border_y;
}

void CompositorPrivate::ProcessInteractiveMotion() {
  // A destroyed grab target returns the cursor to passthrough mode.
  if (grabbed_toplevel_ == nullptr || !grabbed_toplevel_->IsAlive()) {
    ResetCursorMode();
    return;
  }
  // Move the whole scene tree and watch for the top output edge.
  if (cursor_mode_ == CursorMode::kMove) {
    const int x = cursor_->x - grab_x_;
    const int y = cursor_->y - grab_y_;
    if (grabbed_toplevel_->IsXWayland()) {
      wlr_box box = grabbed_toplevel_->Geometry();
      box.x = x;
      box.y = y;
      grabbed_toplevel_->Configure(box);
    } else {
      wlr_scene_node_set_position(&grabbed_toplevel_->scene_tree->node, x, y);
    }
    maximize_on_release_ = CursorAtOutputTop();
    return;
  }

  // Resize only the edges selected by the client request.
  int left = grab_box_.x;
  int right = grab_box_.x + grab_box_.width;
  int top = grab_box_.y;
  int bottom = grab_box_.y + grab_box_.height;
  const int border_x = cursor_->x - grab_x_;
  const int border_y = cursor_->y - grab_y_;
  if (resize_edges_ & WLR_EDGE_TOP) {
    top = std::min(border_y, bottom - 1);
  } else if (resize_edges_ & WLR_EDGE_BOTTOM) {
    bottom = std::max(border_y, top + 1);
  }
  if (resize_edges_ & WLR_EDGE_LEFT) {
    left = std::min(border_x, right - 1);
  } else if (resize_edges_ & WLR_EDGE_RIGHT) {
    right = std::max(border_x, left + 1);
  }

  // Convert the new geometry position back to scene coordinates.
  grabbed_toplevel_->Configure(
      {.x = left, .y = top, .width = right - left, .height = bottom - top});
}

void CompositorPrivate::ProcessCursorMotion(uint32_t time_msec) {
  // Interactive grabs own pointer motion until the button is released.
  if (cursor_mode_ != CursorMode::kPassthrough) {
    ProcessInteractiveMotion();
    return;
  }

  // Find the surface below the cursor in layout coordinates.
  double surface_x = 0;
  double surface_y = 0;
  wlr_surface* surface = nullptr;
  Toplevel* toplevel =
      ToplevelAt(cursor_->x, cursor_->y, &surface, &surface_x, &surface_y);
  const view::Ssd::HitTarget ssd_hit = SsdHitAt(toplevel);
  for (const std::unique_ptr<Toplevel>& candidate : toplevels_) {
    if (candidate->ssd != nullptr) {
      candidate->ssd->SetHovered(
          candidate.get() == toplevel ? ssd_hit : view::Ssd::HitTarget{});
    }
  }
  if (ssd_hit.part != view::Ssd::Part::kNone) {
    wlr_cursor_set_xcursor(cursor_, cursor_manager_,
                           ssd_hit.part == view::Ssd::Part::kResize
                               ? ResizeCursorName(ssd_hit.edges)
                               : "default");
    wlr_seat_pointer_clear_focus(seat_);
    return;
  }
  if (toplevel == nullptr) {
    wlr_cursor_set_xcursor(cursor_, cursor_manager_, "default");
  }
  // Forward motion in surface-local coordinates.
  if (surface != nullptr) {
    wlr_seat_pointer_notify_enter(seat_, surface, surface_x, surface_y);
    wlr_seat_pointer_notify_motion(seat_, time_msec, surface_x, surface_y);
  } else {
    wlr_seat_pointer_clear_focus(seat_);
  }
}

void CompositorPrivate::OnCursorMotion(CompositorPrivate* compositor,
                                       wlr_pointer_motion_event* event) {
  // Relative pointer devices report deltas.
  wlr_cursor_move(compositor->cursor_, &event->pointer->base, event->delta_x,
                  event->delta_y);
  compositor->ProcessCursorMotion(event->time_msec);
}

void CompositorPrivate::OnCursorMotionAbsolute(
    CompositorPrivate* compositor, wlr_pointer_motion_absolute_event* event) {
  // Absolute devices report normalized output coordinates.
  wlr_cursor_warp_absolute(compositor->cursor_, &event->pointer->base, event->x,
                           event->y);
  compositor->ProcessCursorMotion(event->time_msec);
}

void CompositorPrivate::OnCursorButton(CompositorPrivate* compositor,
                                       wlr_pointer_button_event* event) {
  // WLR mouse release event.
  if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
    for (const std::unique_ptr<Toplevel>& toplevel : compositor->toplevels_) {
      if (toplevel->ssd != nullptr) {
        toplevel->ssd->SetPressed({});
      }
    }
    // If suppress_button_release_ is true then it is triggered by
    // "double click to maximize", just ignore.
    if (compositor->suppress_button_release_) {
      compositor->suppress_button_release_ = false;
    } else {
      // If not just transfer the signal to client.
      wlr_seat_pointer_notify_button(compositor->seat_, event->time_msec,
                                     event->button, event->state);
    }

    // End current interaction. e.g. Window resizing, etc.
    compositor->EndInteractive();
    return;
  }

  double surface_x = 0;
  double surface_y = 0;
  wlr_surface* surface = nullptr;
  Toplevel* toplevel =
      compositor->ToplevelAt(compositor->cursor_->x, compositor->cursor_->y,
                             &surface, &surface_x, &surface_y);
  const view::Ssd::HitTarget ssd_hit = compositor->SsdHitAt(toplevel);

  if (ssd_hit.part != view::Ssd::Part::kNone) {
    compositor->FocusToplevel(toplevel);
    compositor->suppress_button_release_ = true;
    if (event->button != BTN_LEFT) {
      return;
    }

    toplevel->ssd->SetPressed(ssd_hit);
    if (ssd_hit.part == view::Ssd::Part::kClose) {
      compositor->last_click_toplevel_ = nullptr;
      wlr_xdg_toplevel_send_close(toplevel->handle);
      return;
    }
    if (ssd_hit.part == view::Ssd::Part::kMinimize) {
      compositor->last_click_toplevel_ = nullptr;
      compositor->Minimize(toplevel);
      return;
    }
    if (ssd_hit.part == view::Ssd::Part::kMaximize) {
      compositor->last_click_toplevel_ = nullptr;
      compositor->ToggleMaximized(toplevel);
      return;
    }
    if (ssd_hit.part == view::Ssd::Part::kResize) {
      compositor->last_click_toplevel_ = nullptr;
      compositor->BeginInteractive(toplevel, CursorMode::kResize,
                                   ssd_hit.edges);
      return;
    }

    const bool same_toplevel = compositor->last_click_toplevel_ == toplevel;
    const bool within_interval =
        event->time_msec - compositor->last_click_time_msec_ <=
        kDoubleClickIntervalMs;
    const bool within_distance =
        std::abs(compositor->cursor_->x - compositor->last_click_x_) <=
            kDoubleClickDistance &&
        std::abs(compositor->cursor_->y - compositor->last_click_y_) <=
            kDoubleClickDistance;
    if (same_toplevel && within_interval && within_distance) {
      compositor->last_click_toplevel_ = nullptr;
      compositor->EndInteractive();
      compositor->ToggleMaximized(toplevel);
      return;
    }

    compositor->last_click_toplevel_ = toplevel;
    compositor->last_click_time_msec_ = event->time_msec;
    compositor->last_click_x_ = compositor->cursor_->x;
    compositor->last_click_y_ = compositor->cursor_->y;
    compositor->BeginInteractive(toplevel, CursorMode::kMove, 0);
    return;
  }

  // A second nearby titlebar press toggles maximized state.
  const bool titlebar_click =
      event->button == BTN_LEFT &&
      compositor->IsTitlebarPoint(toplevel, surface, surface_y);
  const bool same_toplevel = compositor->last_click_toplevel_ != nullptr &&
                             compositor->last_click_toplevel_ == toplevel;
  const bool within_interval =
      event->time_msec - compositor->last_click_time_msec_ <=
      kDoubleClickIntervalMs;
  const bool within_distance =
      std::abs(compositor->cursor_->x - compositor->last_click_x_) <=
          kDoubleClickDistance &&
      std::abs(compositor->cursor_->y - compositor->last_click_y_) <=
          kDoubleClickDistance;

  // Handle the second click here and keep it away from the client.
  if (titlebar_click && same_toplevel && within_interval && within_distance) {
    compositor->last_click_toplevel_ = nullptr;
    compositor->suppress_button_release_ = true;
    compositor->EndInteractive();
    compositor->ToggleMaximized(toplevel);
    compositor->FocusToplevel(toplevel);
    return;
  }

  // Remember only titlebar presses as possible first clicks.
  if (titlebar_click) {
    compositor->last_click_toplevel_ = toplevel;
    compositor->last_click_time_msec_ = event->time_msec;
    compositor->last_click_x_ = compositor->cursor_->x;
    compositor->last_click_y_ = compositor->cursor_->y;
  } else {
    compositor->last_click_toplevel_ = nullptr;
  }

  // Regular presses go to the client before the window takes focus.
  wlr_seat_pointer_notify_button(compositor->seat_, event->time_msec,
                                 event->button, event->state);
  if (toplevel != nullptr) {
    compositor->FocusToplevel(toplevel);
  } else {
    compositor->FocusLayerSurface(compositor->LayerSurfaceFor(surface));
  }
}

void CompositorPrivate::OnCursorAxis(CompositorPrivate* compositor,
                                     wlr_pointer_axis_event* event) {
  // Forward wheel and touchpad axis events without changing their source.
  wlr_seat_pointer_notify_axis(
      compositor->seat_, event->time_msec, event->orientation, event->delta,
      event->delta_discrete, event->source, event->relative_direction);
}

void CompositorPrivate::OnCursorFrame(CompositorPrivate* compositor, void*) {
  // Group all pointer events received in the current backend frame.
  wlr_seat_pointer_notify_frame(compositor->seat_);
}

void CompositorPrivate::OnRequestCursor(
    CompositorPrivate* compositor,
    wlr_seat_pointer_request_set_cursor_event* event) {
  // Only the focused client is allowed to replace the cursor surface.
  if (compositor->seat_->pointer_state.focused_client == event->seat_client) {
    wlr_cursor_set_surface(compositor->cursor_, event->surface,
                           event->hotspot_x, event->hotspot_y);
  }
}

void CompositorPrivate::OnPointerFocusChange(
    CompositorPrivate* compositor, wlr_seat_pointer_focus_change_event* event) {
  // Restore the default cursor after leaving all client surfaces.
  if (event->new_surface == nullptr) {
    wlr_cursor_set_xcursor(compositor->cursor_, compositor->cursor_manager_,
                           "default");
  }
}

void CompositorPrivate::OnRequestSelection(
    CompositorPrivate* compositor,
    wlr_seat_request_set_selection_event* event) {
  // The seat validates and owns the regular clipboard source.
  wlr_seat_set_selection(compositor->seat_, event->source, event->serial);
}

void CompositorPrivate::OnRequestPrimarySelection(
    CompositorPrivate* compositor,
    wlr_seat_request_set_primary_selection_event* event) {
  // Primary selection is handled separately from the regular clipboard.
  wlr_seat_set_primary_selection(compositor->seat_, event->source,
                                 event->serial);
}

void CompositorPrivate::OnNewOutput(CompositorPrivate* compositor,
                                    wlr_output* output) {
  // Got new WL_OUTPUT, start by initializing the render.
  if (!wlr_output_init_render(output, compositor->allocator_,
                              compositor->renderer_)) {
    ABSL_LOG(ERROR) << "Failed to initialize output render.";
    return;
  }

  // Connect output state to WM.
  auto state = std::make_unique<Output>(compositor, output);
  state->frame.Connect(&output->events.frame);
  state->request_state.Connect(&output->events.request_state);
  state->destroy.Connect(&output->events.destroy);

  wlr_output_state output_state;
  wlr_output_state_init(&output_state);
  wlr_output_state_set_enabled(&output_state, true);
  if (wlr_output_mode* mode = wlr_output_preferred_mode(output);
      mode != nullptr) {
    wlr_output_state_set_mode(&output_state, mode);
  }

  // Now commit state.
  const bool committed = wlr_output_commit_state(output, &output_state);
  wlr_output_state_finish(&output_state);
  if (!committed) {
    ABSL_LOG(ERROR) << "Failed to enable output " << output->name;
    return;
  }

  // Add the enabled output to both layout and scene output layout.
  wlr_output_layout_output* layout_output =
      wlr_output_layout_add_auto(compositor->output_layout_, output);
  state->scene_output = wlr_scene_output_create(compositor->scene_, output);
  if (layout_output == nullptr || state->scene_output == nullptr) {
    ABSL_LOG(ERROR) << "Failed to add output to scene layout";
    return;
  }
  wlr_scene_output_layout_add_output(compositor->scene_layout_, layout_output,
                                     state->scene_output);
  if (!state->CreateLayerTrees()) {
    ABSL_LOG(ERROR) << "Failed to create output layer trees";
    return;
  }

  ABSL_LOG(INFO) << "New output is now enabled: " << output->name;
  Output* output_wrapper = state.get();
  compositor->outputs_.push_back(std::move(state));
  compositor->ArrangeLayers(output_wrapper);
}

void CompositorPrivate::OnNewToplevel(CompositorPrivate* compositor,
                                      wlr_xdg_toplevel* handle) {
  // Got new XDG toplevel. Let's start handling by creating its own scene tree.
  auto toplevel = std::make_unique<Toplevel>(compositor, handle);
  toplevel->scene_tree =
      wlr_scene_xdg_surface_create(compositor->toplevel_tree_, handle->base);
  if (toplevel->scene_tree == nullptr) {
    ABSL_LOG(ERROR) << "Failed to create scene tree for XDG_TOPLEVEL";
    return;
  }
  toplevel->scene_tree->node.data = toplevel.get();
  handle->base->data = toplevel->scene_tree;

  // Connect lifecycle and window management requests.
  toplevel->map.Connect(&handle->base->surface->events.map);
  toplevel->unmap.Connect(&handle->base->surface->events.unmap);
  toplevel->commit.Connect(&handle->base->surface->events.commit);
  toplevel->destroy.Connect(&handle->events.destroy);
  toplevel->request_move.Connect(&handle->events.request_move);
  toplevel->request_resize.Connect(&handle->events.request_resize);
  toplevel->request_maximize.Connect(&handle->events.request_maximize);
  toplevel->request_minimize.Connect(&handle->events.request_minimize);
  toplevel->request_fullscreen.Connect(&handle->events.request_fullscreen);
  toplevel->set_title.Connect(&handle->events.set_title);
  toplevel->set_app_id.Connect(&handle->events.set_app_id);
  toplevel->set_parent.Connect(&handle->events.set_parent);
  compositor->toplevels_.push_back(std::move(toplevel));
}

void CompositorPrivate::OnNewXdgDecoration(
    CompositorPrivate* compositor, wlr_xdg_toplevel_decoration_v1* decoration) {
  auto state = std::make_unique<XdgDecoration>(compositor, decoration);
  decoration->data = state.get();
  state->request_mode.Connect(&decoration->events.request_mode);
  state->destroy.Connect(&decoration->events.destroy);
  state->ApplyMode();
  compositor->xdg_decorations_.push_back(std::move(state));
}

void CompositorPrivate::OnNewPopup(CompositorPrivate* compositor,
                                   wlr_xdg_popup* handle) {
  // Got new XDG popup. Let's start handling by finding its parent.
  if (handle->parent == nullptr) {
    return;
  }
  auto popup = std::make_unique<Popup>(compositor, handle);
  wlr_xdg_surface* parent =
      wlr_xdg_surface_try_from_wlr_surface(handle->parent);
  wlr_scene_tree* parent_tree =
      parent == nullptr ? nullptr : static_cast<wlr_scene_tree*>(parent->data);
  if (parent_tree == nullptr) {
    ABSL_LOG(ERROR) << absl::StrCat("Failed to find the parent scene tree ",
                                    "for the new XDG_POPUP.");
    return;
  }

  // Now handle the popup's own scene tree.
  popup->scene_tree = wlr_scene_xdg_surface_create(parent_tree, handle->base);
  if (popup->scene_tree == nullptr) {
    ABSL_LOG(ERROR) << "Failed to create scene tree for XDG_POPUP";
    return;
  }

  // Insert this popup as a child into its parent scene tree.
  handle->base->data = popup->scene_tree;
  popup->commit.Connect(&handle->base->surface->events.commit);
  popup->destroy.Connect(&handle->events.destroy);
  compositor->popups_.push_back(std::move(popup));
}

int CompositorPrivate::OnTerminateSignal(int signal, void* data) {
  // Handle SIGTERM.
  ABSL_LOG(INFO) << "Received signal " << signal << ", shutting down!";
  static_cast<CompositorPrivate*>(data)->Stop();
  return 0;
}

int CompositorPrivate::OnQtFrameTimer(void* data) {
  auto* compositor = static_cast<CompositorPrivate*>(data);
  QCoreApplication::processEvents(QEventLoop::AllEvents);
  for (const std::unique_ptr<Toplevel>& toplevel : compositor->toplevels_) {
    if (toplevel->ssd != nullptr) {
      toplevel->ssd->Render();
    }
  }
  return compositor->qt_frame_timer_ == nullptr
             ? 0
             : wl_event_source_timer_update(compositor->qt_frame_timer_, 16);
}

bool CompositorPrivate::ConfigureBackendEnvironment(
    const utils::StartupArgs& startup_args) const {
  // If the WM is NOT nested then we don't have to do anything.
  if (!startup_args.nested) {
    return true;
  }

  // If WAYLAND_DISPLAY or WAYLAND_SOCKET is set (Wlr backend) then use them.
  if (std::getenv("WAYLAND_DISPLAY") != nullptr ||
      std::getenv("WAYLAND_SOCKET") != nullptr) {
    setenv("WLR_BACKENDS", "wayland", 1);
    return true;
  }

  // If legacy X11 display is spefcified (X11 backend) then use them.
  if (std::getenv("DISPLAY") != nullptr) {
    setenv("WLR_BACKENDS", "x11", 1);
    return true;
  }

  // If nothing is set, then we have a problem.
  ABSL_LOG(ERROR) << "Nested mode requires WAYLAND_DISPLAY or DISPLAY!";
  return false;
}

bool CompositorPrivate::Spawn(const std::string& command) const {
  // Fork a child process for user session.
  // UNIX fork(): -1 means fail. 0 is for child process. >0 means PID for child
  //              process.
  const pid_t child = fork();
  if (child < 0) {
    ABSL_LOG(ERROR) << "Failed to start user session: " << std::strerror(errno);
    return false;
  }

  // Case A: A PID is returned and this is the parent process.
  if (child != 0) {
    return true;
  }

  // Case B: 0 is returned, meaning we are inside the child process.
  //         Then just kickstart the session command. First handle the signals.
  sigset_t empty_mask;
  sigemptyset(&empty_mask);
  sigprocmask(SIG_SETMASK, &empty_mask, nullptr);
  std::signal(SIGCHLD, SIG_DFL);
  std::signal(SIGINT, SIG_DFL);
  std::signal(SIGTERM, SIG_DFL);
  std::signal(SIGPIPE, SIG_DFL);

  // The compositor uses QtQuick's software adaptation for its own buffers.
  // Do not force that choice onto the session.
  unsetenv("QT_QUICK_BACKEND");

  // Start user session using sh.
  execl("/bin/sh", "/bin/sh", "-c", command.c_str(),
        static_cast<char*>(nullptr));

  // Never reaches this line if the session process runs.
  // If reached, then session process failed to start, the child
  // process exits.
  _exit(EX_UNAVAILABLE);
}

bool CompositorPrivate::Fail(const char* message) const {
  // Startup helpers use one common failure path.
  ABSL_LOG(ERROR) << message;
  return false;
}

void CompositorPrivate::Destroy() {
  ABSL_LOG(INFO) << "FlakeWM core destructor is called.";
  Stop();

  // Cleanup Wayland event source
  for (wl_event_source*& source : signal_sources_) {
    if (source != nullptr) {
      wl_event_source_remove(source);
      source = nullptr;
    }
  }

  // Disconnect all signal listeners
  new_layer_surface_.Disconnect();
  new_xdg_decoration_.Disconnect();
  new_popup_.Disconnect();
  new_toplevel_.Disconnect();
  new_output_.Disconnect();
  new_input_.Disconnect();
  request_primary_selection_.Disconnect();
  request_selection_.Disconnect();
  pointer_focus_change_.Disconnect();
  request_cursor_.Disconnect();
  cursor_frame_.Disconnect();
  cursor_axis_.Disconnect();
  cursor_button_.Disconnect();
  cursor_motion_absolute_.Disconnect();
  cursor_motion_.Disconnect();

  if (qt_frame_timer_ != nullptr) {
    wl_event_source_remove(qt_frame_timer_);
    qt_frame_timer_ = nullptr;
  }

  // Drop local protocol wrappers while their wlroots objects still exist.
  ResetCursorMode();
  xdg_decorations_.clear();
  popups_.clear();
  layer_surfaces_.clear();
  toplevels_.clear();
  keyboards_.clear();
  outputs_.clear();

  // The XWM owns a Wayland client.
  // This also needs to be stopped.
  xwayland_.reset();
  unsetenv("DISPLAY");
  if (display_ != nullptr) {
    wl_display_destroy_clients(display_);
  }

  // Clear scene nodes.
  if (scene_ != nullptr) {
    wlr_scene_node_destroy(&scene_->tree.node);
    scene_ = nullptr;
    scene_layout_ = nullptr;
    shell_layer_trees_.fill(nullptr);
    toplevel_tree_ = nullptr;
  }

  // Clear cursor manager.
  if (cursor_manager_ != nullptr) {
    wlr_xcursor_manager_destroy(cursor_manager_);
    cursor_manager_ = nullptr;
  }

  // Release the cursor.
  if (cursor_ != nullptr) {
    wlr_cursor_destroy(cursor_);
    cursor_ = nullptr;
  }

  // Release allocator.
  if (allocator_ != nullptr) {
    wlr_allocator_destroy(allocator_);
    allocator_ = nullptr;
  }

  // Clear renders.
  if (renderer_ != nullptr) {
    wlr_renderer_destroy(renderer_);
    renderer_ = nullptr;
  }

  // Clear Wlroots display backends.
  if (backend_ != nullptr) {
    wlr_backend_destroy(backend_);
    backend_ = nullptr;
  }

  // Clear Wayland display.
  if (display_ != nullptr) {
    wl_display_destroy(display_);
    display_ = nullptr;
    compositor_ = nullptr;
    layer_shell_ = nullptr;
    xdg_shell_ = nullptr;
    xdg_decoration_manager_ = nullptr;
  }
}

}  // namespace core
}  // namespace flakewm
