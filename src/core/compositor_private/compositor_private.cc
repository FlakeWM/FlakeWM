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
#include <string_view>
#include <utility>
#include <vector>

#include "src/dbus/wlcom/wlcom_dbus_manager.h"
#include "src/render/backdrop_blur_renderer.h"
#include "src/utils/args_handler/args_handler.h"
#include "src/utils/misc/misc.h"
#include "src/xwayland/xwayland_manager/xwayland_manager.h"

namespace flakewm {
namespace core {
namespace {

constexpr std::size_t kMaximumClientBufferSize = std::size_t{1024} * 1024;
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

// A security-context child is an ordinary sandboxed application. Keep this
// allow-list deliberately narrow: newly-added globals remain unavailable until
// they have been reviewed. Privileged desktop services connect through the
// compositor's regular socket and retain the full registry.
constexpr auto kAllowedSecurityContextGlobals =
    std::to_array<std::string_view>({
        "wl_shm",
        "wl_drm",
        "zwp_linux_dmabuf_v1",
        "wp_linux_drm_syncobj_manager_v1",
        "wl_compositor",
        "wl_subcompositor",
        "wl_data_device_manager",
        "zwp_primary_selection_device_manager_v1",
        "wp_viewporter",
        "wp_fractional_scale_manager_v1",
        "wp_presentation",
        "wp_tearing_control_manager_v1",
        "wl_output",
        "wp_color_manager_v1",
        "xdg_wm_base",
        "zxdg_decoration_manager_v1",
        "xdg_activation_v1",
        "xdg_wm_dialog_v1",
        "zwp_relative_pointer_manager_v1",
        "zwp_pointer_constraints_v1",
        "zwp_pointer_gestures_v1",
        "zwp_tablet_manager_v2",
        "zwp_idle_inhibit_manager_v1",
        "zwp_keyboard_shortcuts_inhibit_manager_v1",
        "zwp_input_timestamps_manager_v1",
        "zwp_text_input_manager_v1",
        "zwp_text_input_manager_v2",
        "zwp_text_input_manager_v3",
        "zxdg_output_manager_v1",
    });

bool FilterSecurityContextGlobal(const wl_client* client,
                                 const wl_global* global, void* data) {
  auto* manager = static_cast<wlr_security_context_manager_v1*>(data);
  const wl_interface* interface = wl_global_get_interface(global);
  if (manager == nullptr || interface == nullptr ||
      wlr_security_context_manager_v1_lookup_client(manager, client) ==
          nullptr) {
    return true;
  }
  const std::string_view name(interface->name);
  return std::find(kAllowedSecurityContextGlobals.begin(),
                   kAllowedSecurityContextGlobals.end(),
                   name) != kAllowedSecurityContextGlobals.end();
}

wlr_renderer* CreateRenderer(wlr_backend* backend) {
  // Keep explicit overrides useful for debugging and driver workarounds.
  if (std::getenv("WLR_RENDERER") != nullptr) {
    return wlr_renderer_autocreate(backend);
  }

  // GLES2 is the default; Vulkan (opt in with WLR_RENDERER=vulkan) is only
  // tried when GLES2 is unavailable.
  if (setenv("WLR_RENDERER", "gles2", 1) == 0) {
    if (wlr_renderer* renderer = wlr_renderer_autocreate(backend);
        renderer != nullptr) {
      unsetenv("WLR_RENDERER");
      ABSL_LOG(INFO) << "Using GLES2 renderer";
      return renderer;
    }
  }

  ABSL_LOG(WARNING) << "GLES2 renderer unavailable, falling back to Vulkan";
  setenv("WLR_RENDERER", "vulkan", 1);
  if (wlr_renderer* renderer = wlr_renderer_autocreate(backend);
      renderer != nullptr) {
    unsetenv("WLR_RENDERER");
    return renderer;
  }

  ABSL_LOG(WARNING) << "Vulkan renderer unavailable, falling back to Pixman";
  setenv("WLR_RENDERER", "pixman", 1);
  wlr_renderer* renderer = wlr_renderer_autocreate(backend);
  unsetenv("WLR_RENDERER");
  return renderer;
}

wlr_output_mode* HighestNativeRefreshMode(wlr_output* output) {
  // Keep the EDID-preferred resolution and raise only its refresh rate.
  wlr_output_mode* selected = wlr_output_preferred_mode(output);
  if (selected == nullptr) {
    return nullptr;
  }

  wlr_output_mode* mode = nullptr;
  wl_list_for_each(mode, &output->modes, link) {
    if (mode->width == selected->width && mode->height == selected->height &&
        mode->refresh > selected->refresh) {
      selected = mode;
    }
  }
  return selected;
}

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

constexpr int kCsdResizeMargin = 8;
// Height of a moved window's frame kept above the bottom of the usable area.
constexpr int kMoveTitlebarVisible = 32;

// Splits a usable area into the slot of a split-screen tile.
wlr_box TileBox(const wlr_box& usable, view::SplitScreenSwitcher::Tile tile) {
  const int left_width = usable.width / 2;
  const int right_width = usable.width - left_width;
  const int top_height = usable.height / 2;
  const int bottom_height = usable.height - top_height;
  wlr_box target = usable;
  switch (tile) {
    case view::SplitScreenSwitcher::Tile::kLeft:
      target.width = left_width;
      break;
    case view::SplitScreenSwitcher::Tile::kRight:
      target.x += left_width;
      target.width = right_width;
      break;
    case view::SplitScreenSwitcher::Tile::kTopLeft:
      target.width = left_width;
      target.height = top_height;
      break;
    case view::SplitScreenSwitcher::Tile::kBottomLeft:
      target.y += top_height;
      target.width = left_width;
      target.height = bottom_height;
      break;
    case view::SplitScreenSwitcher::Tile::kTopRight:
      target.x += left_width;
      target.width = right_width;
      target.height = top_height;
      break;
    case view::SplitScreenSwitcher::Tile::kBottomRight:
      target.x += left_width;
      target.y += top_height;
      target.width = right_width;
      target.height = bottom_height;
      break;
  }
  return target;
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
  backend_owner_ =
      backend::Backend::Create(wl_display_get_event_loop(display_));
  if (backend_owner_ == nullptr) {
    return Fail("Failed to create Wlroots backend.");
  }
  backend_ = backend_owner_->Handle();

  // Prepare renderer and allocator for output buffers.
  wlr_renderer* renderer = CreateRenderer(backend_);
  if (renderer == nullptr) {
    return Fail("Failed to create Wlroots renderer.");
  }
  backdrop_blur_renderer_ = render::BackdropBlurRenderer::Create(renderer);
  renderer_ = backdrop_blur_renderer_ == nullptr
                  ? renderer
                  : backdrop_blur_renderer_->Handle();

  if (!wlr_renderer_init_wl_display(renderer_, display_)) {
    return Fail("Failed to initialize renderer globals.");
  }

  const int renderer_drm_fd = wlr_renderer_get_drm_fd(renderer_);
  if (renderer_drm_fd >= 0 && renderer_->features.timeline &&
      backend_->features.timeline) {
    explicit_sync_manager_ =
        wlr_linux_drm_syncobj_manager_v1_create(display_, 1, renderer_drm_fd);
    if (explicit_sync_manager_ == nullptr) {
      return Fail("Failed to create Linux DRM syncobj manager");
    }
    ABSL_LOG(INFO) << "Linux explicit synchronization enabled";
  } else {
    ABSL_LOG(INFO) << "Linux explicit synchronization unavailable: timeline "
                      "support is incomplete";
  }

  allocator_ = wlr_allocator_autocreate(backend_, renderer_);
  if (allocator_ == nullptr) {
    return Fail("Failed to create wlroots allocator.");
  }
  if (backdrop_blur_renderer_ != nullptr) {
    backdrop_blur_renderer_->SetAllocator(allocator_);
  }

  // Publish basic globals required by regular Wayland clients.
  compositor_ = wlr_compositor_create(display_, 6, renderer_);
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
  security_context_manager_ = wlr_security_context_manager_v1_create(display_);
  if (security_context_manager_ == nullptr) {
    return Fail("Failed to create security-context manager");
  }
  wl_display_set_global_filter(display_, FilterSecurityContextGlobal,
                               security_context_manager_);
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
  scene_direct_scanout_default_ = scene_->WLR_PRIVATE.direct_scanout;
  scene_calculate_visibility_default_ =
      scene_->WLR_PRIVATE.calculate_visibility;

  gamma_control_manager_ = wlr_gamma_control_manager_v1_create(display_);
  if (gamma_control_manager_ == nullptr) {
    return Fail("Failed to create gamma-control manager");
  }
  wlr_scene_set_gamma_control_manager_v1(scene_, gamma_control_manager_);

  if (renderer_->features.input_color_transform) {
    size_t transfer_functions_length = 0;
    wp_color_manager_v1_transfer_function* transfer_functions =
        wlr_color_manager_v1_transfer_function_list_from_renderer(
            renderer_, &transfer_functions_length);
    size_t primaries_length = 0;
    wp_color_manager_v1_primaries* primaries =
        wlr_color_manager_v1_primaries_list_from_renderer(renderer_,
                                                          &primaries_length);
    constexpr wp_color_manager_v1_render_intent render_intents[] = {
        WP_COLOR_MANAGER_V1_RENDER_INTENT_PERCEPTUAL,
    };
    const wlr_color_manager_v1_options options = {
        .features =
            {
                .icc_v2_v4 = false,
                .parametric = true,
                .set_primaries = false,
                .set_tf_power = false,
                .set_luminances = false,
                .set_mastering_display_primaries = true,
                .extended_target_volume = false,
                .windows_scrgb = false,
            },
        .render_intents = render_intents,
        .render_intents_len = std::size(render_intents),
        .transfer_functions = transfer_functions,
        .transfer_functions_len = transfer_functions_length,
        .primaries = primaries,
        .primaries_len = primaries_length,
    };
    color_manager_ = wlr_color_manager_v1_create(display_, 2, &options);
    std::free(transfer_functions);
    std::free(primaries);
    if (color_manager_ == nullptr) {
      return Fail("Failed to create color-management manager");
    }
    wlr_scene_set_color_manager_v1(scene_, color_manager_);
  } else {
    ABSL_LOG(WARNING) << "Color management unavailable with this renderer";
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
  touch_feedback_ = view::TouchFeedback::Create(&scene_->tree);
  if (touch_feedback_ == nullptr) {
    ABSL_LOG(WARNING) << "Touch feedback QML is unavailable";
  }
  session_lock_tree_ = wlr_scene_tree_create(&scene_->tree);
  if (session_lock_tree_ == nullptr) {
    return Fail("Failed to create the secure session-lock layer");
  }
  wlr_scene_node_set_enabled(&session_lock_tree_->node, false);

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
  virtual_keyboard_manager_ = wlr_virtual_keyboard_manager_v1_create(display_);
  if (virtual_keyboard_manager_ == nullptr) {
    return Fail("Failed to create virtual-keyboard manager");
  }
  input_method_relay_ = std::make_unique<protocol::InputMethodRelay>(
      display_, seat_, scene_,
      shell_layer_trees_[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY], output_layout_);
  if (!input_method_relay_->IsValid()) {
    return Fail("Failed to create text-input and input-method globals");
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
  SetCursorName("default");
  // GXDE mouse finder; enabled by the wlcom D-Bus layer from its effect state.
  shake_cursor_ = std::make_unique<view::ShakeCursor>(
      wl_display_get_event_loop(display_), &scene_->tree, cursor_,
      [this]() { return cursor_manager_; },
      [this](bool locked) { LockCursorImage(locked); });

  window_selector_ = std::make_unique<view::WindowSelector>(
      shell_layer_trees_[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY], seat_, cursor_,
      [this](const char* name) { SetCursorName(name); },
      [this](view::WindowSelector::Mode mode, double layout_x, double layout_y,
             wlr_surface* mask) -> std::optional<view::WindowSelector::Target> {
        wlr_output* output =
            wlr_output_layout_output_at(output_layout_, layout_x, layout_y);
        if (output == nullptr) return std::nullopt;
        wlr_box output_box = {};
        wlr_output_layout_get_box(output_layout_, output, &output_box);
        const auto output_target = [&]() {
          return view::WindowSelector::Target{
              .output = output, .surface = nullptr, .box = output_box};
        };
        if (mode == view::WindowSelector::Mode::kOutput) {
          return output_target();
        }

        const wlr_surface* mask_root =
            mask == nullptr ? nullptr : wlr_surface_get_root_surface(mask);
        const auto find_window = [&](wlr_scene_tree* parent)
            -> std::optional<view::WindowSelector::Target> {
          wlr_scene_node* node = nullptr;
          wl_list_for_each_reverse(node, &parent->children, link) {
            const auto candidate_iterator = std::find_if(
                toplevels_.begin(), toplevels_.end(),
                [node](const std::unique_ptr<Toplevel>& candidate) {
                  return candidate.get() == node->data;
                });
            if (candidate_iterator == toplevels_.end()) continue;
            Toplevel* candidate = candidate_iterator->get();
            if (!candidate->mapped || !candidate->IsAlive() ||
                candidate->scene_tree == nullptr ||
                candidate->Surface() == nullptr ||
                wlr_surface_get_root_surface(candidate->Surface()) ==
                    mask_root) {
              continue;
            }
            int tree_x = 0;
            int tree_y = 0;
            if (!wlr_scene_node_coords(&candidate->scene_tree->node, &tree_x,
                                       &tree_y)) {
              continue;
            }
            wlr_box box = candidate->FrameGeometry();
            box.x += tree_x;
            box.y += tree_y;
            if (!wlr_box_contains_point(&box, layout_x, layout_y)) continue;
            wlr_box clipped = {};
            if (!wlr_box_intersection(&clipped, &box, &output_box)) continue;
            return view::WindowSelector::Target{
                .output = output,
                .surface = candidate->Surface(),
                .box = clipped,
            };
          }
          return std::nullopt;
        };
        if (const auto overlay_window = find_window(
                shell_layer_trees_[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY]);
            overlay_window.has_value()) {
          return overlay_window;
        }
        if (const auto window = find_window(toplevel_tree_);
            window.has_value()) {
          return window;
        }
        // Region selection follows Treeland: a click over empty space selects
        // the containing output, while a click over a window uses its bounds.
        return mode == view::WindowSelector::Mode::kRegion
                   ? std::optional<view::WindowSelector::Target>(
                         output_target())
                   : std::nullopt;
      });

  app_switcher_ = std::make_unique<view::AppSwitcher>(
      shell_layer_trees_[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY],
      [this]() {
        std::vector<view::AppSwitcher::Entry> entries;
        const wlr_surface* focused = seat_->keyboard_state.focused_surface;
        entries.reserve(toplevels_.size());
        for (const std::unique_ptr<Toplevel>& toplevel : toplevels_) {
          if (!toplevel->mapped || !toplevel->IsAlive() ||
              !toplevel->WantsFocus() || toplevel->Surface() == nullptr ||
              (!toplevel->all_workspaces &&
               toplevel->workspace != current_workspace_)) {
            continue;
          }
          entries.push_back({
              .surface = toplevel->Surface(),
              .title = QString::fromUtf8(
                  toplevel->Title() == nullptr ? "" : toplevel->Title()),
              .app_id = QString::fromUtf8(
                  toplevel->AppId() == nullptr ? "" : toplevel->AppId()),
              .active = focused == toplevel->Surface(),
              .minimized = toplevel->minimized,
          });
        }
        return entries;
      },
      [this](wlr_surface* surface) {
        Toplevel* toplevel = ToplevelForSurface(surface);
        if (toplevel == nullptr || !toplevel->mapped || !toplevel->IsAlive()) {
          return;
        }
        if (toplevel->minimized) {
          toplevel->minimized = false;
          toplevel->SetMinimizedState(false);
          if (toplevel->scene_tree != nullptr &&
              (toplevel->all_workspaces ||
               toplevel->workspace == current_workspace_)) {
            wlr_scene_node_set_enabled(&toplevel->scene_tree->node, true);
          }
        }
        FocusToplevel(toplevel);
      },
      [this]() {
        wlr_box box = {};
        wlr_output* output =
            wlr_output_layout_output_at(output_layout_, cursor_->x, cursor_->y);
        wlr_output_layout_get_box(output_layout_, output, &box);
        return box;
      },
      [this](const void* owner, wlr_texture* texture,
             const pixman_region32_t* region, float offset) {
        if (backdrop_blur_renderer_ == nullptr ||
            !backdrop_blur_renderer_->IsSupported()) {
          return false;
        }
        backdrop_blur_renderer_->SetTextureBlur(owner, texture, region, offset);
        UpdateBackdropBlurState();
        return true;
      },
      [this](const void* owner) {
        if (backdrop_blur_renderer_ == nullptr) return;
        backdrop_blur_renderer_->ClearTextureBlur(owner);
        UpdateBackdropBlurState();
      });

  multitasking_ = std::make_unique<view::Multitasking>(
      shell_layer_trees_[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY],
      [this]() {
        std::vector<view::Multitasking::Entry> entries;
        const wlr_surface* focused = seat_->keyboard_state.focused_surface;
        entries.reserve(toplevels_.size());
        for (const std::unique_ptr<Toplevel>& toplevel : toplevels_) {
          if (!toplevel->mapped || !toplevel->IsAlive() ||
              !toplevel->WantsFocus() || toplevel->Surface() == nullptr) {
            continue;
          }
          wlr_box geometry = toplevel->Geometry();
          if (toplevel->scene_tree != nullptr) {
            geometry.x += toplevel->scene_tree->node.x;
            geometry.y += toplevel->scene_tree->node.y;
          }
          entries.push_back({
              .surface = toplevel->Surface(),
              .title = QString::fromUtf8(
                  toplevel->Title() == nullptr ? "" : toplevel->Title()),
              .app_id = QString::fromUtf8(
                  toplevel->AppId() == nullptr ? "" : toplevel->AppId()),
              .geometry = geometry,
              .workspace = toplevel->workspace,
              .active = focused == toplevel->Surface(),
              .minimized = toplevel->minimized,
              .kept_above = toplevel->kept_above,
          });
        }
        return entries;
      },
      [this]() { return workspace_count_; },
      [this]() { return current_workspace_; },
      [this](int workspace) { SwitchWorkspace(workspace); },
      [this](wlr_surface* surface) {
        Toplevel* toplevel = ToplevelForSurface(surface);
        if (toplevel == nullptr || !toplevel->mapped || !toplevel->IsAlive()) {
          return;
        }
        SwitchWorkspace(toplevel->workspace);
        if (toplevel->minimized) {
          toplevel->minimized = false;
          toplevel->SetMinimizedState(false);
          if (toplevel->scene_tree != nullptr) {
            wlr_scene_node_set_enabled(&toplevel->scene_tree->node, true);
          }
        }
        FocusToplevel(toplevel);
      },
      [this](wlr_surface* surface) {
        Toplevel* toplevel = ToplevelForSurface(surface);
        if (toplevel != nullptr && toplevel->IsAlive()) toplevel->Close();
      },
      [this](wlr_surface* surface) {
        if (Toplevel* toplevel = ToplevelForSurface(surface);
            toplevel != nullptr && toplevel->IsAlive()) {
          SetKeptAbove(toplevel, !toplevel->kept_above);
        }
      },
      [this]() { return AddWorkspace(); },
      [this](int workspace) { return RemoveWorkspace(workspace); },
      [this](int from, int to) { return ReorderWorkspace(from, to); },
      [this](wlr_surface* surface, int workspace) {
        return MoveToplevelToWorkspace(ToplevelForSurface(surface), workspace);
      },
      [this](bool hidden) { SetMultitaskingSourcesHidden(hidden); },
      [this]() {
        wlr_box box = {};
        wlr_output* output =
            wlr_output_layout_output_at(output_layout_, cursor_->x, cursor_->y);
        wlr_output_layout_get_box(output_layout_, output, &box);
        return box;
      });

  window_previews_ = std::make_unique<view::WindowPreviews>(
      shell_layer_trees_[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY],
      [this]() {
        std::vector<view::WindowPreviews::Entry> entries;
        const wlr_surface* focused = seat_->keyboard_state.focused_surface;
        entries.reserve(toplevels_.size());
        for (const std::unique_ptr<Toplevel>& toplevel : toplevels_) {
          if (!toplevel->mapped || !toplevel->IsAlive() ||
              !toplevel->CanManage() || !toplevel->WantsFocus() ||
              toplevel->Surface() == nullptr) {
            continue;
          }
          wlr_box geometry = toplevel->Geometry();
          if (toplevel->scene_tree != nullptr) {
            geometry.x += toplevel->scene_tree->node.x;
            geometry.y += toplevel->scene_tree->node.y;
          }
          entries.push_back({
              .surface = toplevel->Surface(),
              .title = QString::fromUtf8(
                  toplevel->Title() == nullptr ? "" : toplevel->Title()),
              .app_id = QString::fromUtf8(
                  toplevel->AppId() == nullptr ? "" : toplevel->AppId()),
              .geometry = geometry,
              .workspace = toplevel->workspace,
              .active = focused == toplevel->Surface(),
              .minimized = toplevel->minimized,
          });
        }
        return entries;
      },
      [this]() { return current_workspace_; },
      [this](wlr_surface* surface) {
        Toplevel* toplevel = ToplevelForSurface(surface);
        if (toplevel == nullptr || !toplevel->mapped || !toplevel->IsAlive()) {
          return;
        }
        if (toplevel->minimized) {
          toplevel->minimized = false;
          toplevel->SetMinimizedState(false);
        }
        SwitchWorkspace(toplevel->workspace);
        FocusToplevel(toplevel);
      },
      [this](wlr_surface* surface) {
        if (Toplevel* toplevel = ToplevelForSurface(surface);
            toplevel != nullptr && toplevel->IsAlive()) {
          toplevel->Close();
        }
      },
      [this](bool hidden) { SetWindowPreviewsSourcesHidden(hidden); },
      [this]() {
        wlr_box box = {};
        wlr_output* output =
            wlr_output_layout_output_at(output_layout_, cursor_->x, cursor_->y);
        if (output != nullptr) {
          wlr_output_layout_get_box(output_layout_, output, &box);
        }
        return box;
      },
      [this]() {
        wlr_output* output =
            wlr_output_layout_output_at(output_layout_, cursor_->x, cursor_->y);
        return output == nullptr ? wlr_box{} : UsableOutputBox(output);
      },
      [this]() { return std::pair<double, double>{cursor_->x, cursor_->y}; });

  window_menu_ = std::make_unique<view::WindowMenu>(
      shell_layer_trees_[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY],
      [this](wlr_surface* surface) -> std::optional<view::WindowMenu::State> {
        Toplevel* toplevel = ToplevelForSurface(surface);
        if (toplevel == nullptr || !toplevel->mapped || !toplevel->IsAlive()) {
          return std::nullopt;
        }
        return view::WindowMenu::State{
            .maximized = toplevel->maximized,
            .minimizable = toplevel->CanMinimize(),
            .maximizable = toplevel->CanMaximize(),
            .movable = toplevel->CanManage(),
            .resizable = toplevel->CanManage() && !toplevel->maximized,
            .kept_above = toplevel->kept_above,
            .all_workspaces = toplevel->all_workspaces,
            .workspace = toplevel->all_workspaces ? current_workspace_
                                                  : toplevel->workspace,
            .workspace_count = workspace_count_,
        };
      },
      [this](wlr_surface* surface, view::WindowMenu::Action action) {
        HandleWindowMenuAction(surface, action);
      },
      [this](double x, double y) { return OutputBoxAt(x, y); },
      [this](const void* owner, wlr_texture* texture,
             const pixman_region32_t* region, float offset) {
        if (backdrop_blur_renderer_ == nullptr ||
            !backdrop_blur_renderer_->IsSupported()) {
          return false;
        }
        backdrop_blur_renderer_->SetTextureBlur(owner, texture, region, offset);
        UpdateBackdropBlurState();
        return true;
      },
      [this](const void* owner) {
        if (backdrop_blur_renderer_ == nullptr) return;
        backdrop_blur_renderer_->ClearTextureBlur(owner);
        UpdateBackdropBlurState();
      });

  const auto set_ssd_popup_blur =
      [this](const void* owner, wlr_texture* texture,
             const pixman_region32_t* region, float offset) {
        if (backdrop_blur_renderer_ == nullptr ||
            !backdrop_blur_renderer_->IsSupported()) {
          return false;
        }
        backdrop_blur_renderer_->SetTextureBlur(owner, texture, region, offset);
        UpdateBackdropBlurState();
        return true;
      };
  const auto clear_ssd_popup_blur = [this](const void* owner) {
    if (backdrop_blur_renderer_ == nullptr) return;
    backdrop_blur_renderer_->ClearTextureBlur(owner);
    UpdateBackdropBlurState();
  };
  titlebar_tooltip_ = std::make_unique<view::TitlebarTooltip>(
      shell_layer_trees_[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY],
      [this](double x, double y) { return OutputBoxAt(x, y); },
      set_ssd_popup_blur, clear_ssd_popup_blur);
  split_screen_switcher_ = std::make_unique<view::SplitScreenSwitcher>(
      shell_layer_trees_[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY],
      [this](wlr_surface* surface, view::SplitScreenSwitcher::Tile tile) {
        TileToplevel(ToplevelForSurface(surface), tile);
      },
      [this](double x, double y) { return OutputBoxAt(x, y); },
      set_ssd_popup_blur, clear_ssd_popup_blur);
  tile_animation_ = std::make_unique<view::TileAnimation>(
      shell_layer_trees_[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY]);

  // Publish those extended protocols ONLY after seat & cursor O.K.
  protocol_manager_ = std::make_unique<protocol::ProtocolManager>(this);
  if (!protocol_manager_->Create(
          display_, backend_, seat_, cursor_,
          shell_layer_trees_[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY],
          session_lock_tree_, output_layout_)) {
    return Fail("Failed to create desktop protocol globals!");
  }

  if (!RegisterDefaultKeyBindings()) {
    return Fail("Failed to register default key bindings");
  }
  dbus_manager_ = std::make_unique<dbus::WlcomDbusManager>(
      this, key_binding_manager_.get());
  if (!dbus_manager_->Start()) {
    ABSL_LOG(WARNING)
        << "GXDE/Kylin compatibility D-Bus is partially unavailable";
  }

  // Connect global object and input event listeners.
  new_output_.Connect(&backend_->events.new_output);
  new_toplevel_.Connect(&xdg_shell_->events.new_toplevel);
  new_popup_.Connect(&xdg_shell_->events.new_popup);
  new_xdg_decoration_.Connect(
      &xdg_decoration_manager_->events.new_toplevel_decoration);
  new_layer_surface_.Connect(&layer_shell_->events.new_surface);
  new_input_.Connect(&backend_->events.new_input);
  new_virtual_keyboard_.Connect(
      &virtual_keyboard_manager_->events.new_virtual_keyboard);
  cursor_motion_.Connect(&cursor_->events.motion);
  cursor_motion_absolute_.Connect(&cursor_->events.motion_absolute);
  cursor_button_.Connect(&cursor_->events.button);
  cursor_axis_.Connect(&cursor_->events.axis);
  cursor_frame_.Connect(&cursor_->events.frame);
  touch_down_.Connect(&cursor_->events.touch_down);
  touch_up_.Connect(&cursor_->events.touch_up);
  touch_motion_.Connect(&cursor_->events.touch_motion);
  touch_cancel_.Connect(&cursor_->events.touch_cancel);
  touch_frame_.Connect(&cursor_->events.touch_frame);
  request_cursor_.Connect(&seat_->events.request_set_cursor);
  pointer_focus_change_.Connect(&seat_->pointer_state.events.focus_change);
  request_selection_.Connect(&seat_->events.request_set_selection);
  selection_persist_ =
      std::make_unique<input::SelectionPersist>(display_, seat_);
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
      wl_event_source_timer_update(qt_frame_timer_, qt_frame_interval_ms_) <
          0) {
    return Fail("Failed to install QtQuick frame timer");
  }

  // Export the child session environment after the socket is ready.
  setenv("WAYLAND_DISPLAY", socket_name_.c_str(), 1);
  unsetenv("WAYLAND_SOCKET");
  setenv("XDG_CURRENT_DESKTOP", "FlakeWM", 1);
  setenv("XDG_SESSION_DESKTOP", "FlakeWM", 1);
  setenv("XDG_SESSION_TYPE", "wayland", 1);
  if (dbus_manager_ != nullptr) {
    dbus_manager_->UpdateActivationEnvironment();
  }

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

void CompositorPrivate::DamageOutputForBackdropBlur(Output* output,
                                                    bool schedule_frame) {
  if (output == nullptr || output->handle == nullptr ||
      output->scene_output == nullptr || output->handle->width <= 0 ||
      output->handle->height <= 0) {
    return;
  }
  pixman_region32_t damage;
  pixman_region32_init_rect(&damage, 0, 0, output->handle->width,
                            output->handle->height);
  wlr_damage_ring_add(&output->scene_output->damage_ring, &damage);
  pixman_region32_union(
      &output->scene_output->WLR_PRIVATE.pending_commit_damage,
      &output->scene_output->WLR_PRIVATE.pending_commit_damage, &damage);
  pixman_region32_fini(&damage);
  if (schedule_frame) {
    wlr_output_schedule_frame(output->handle);
  }
}

void CompositorPrivate::UpdateBackdropBlurState() {
  if (scene_ == nullptr) {
    return;
  }
  const bool active = backdrop_blur_renderer_ != nullptr &&
                      backdrop_blur_renderer_->HasActiveBlur();
  // Rounded corners are cut out at draw time, but the scene graph still treats
  // the whole surface as opaque.
  const bool rounded = backdrop_blur_renderer_ != nullptr &&
                       backdrop_blur_renderer_->HasRoundedCorners();
  // Callers re-register blurs on every commit (treeland personalization does,
  // so a blurred DTK6 window's region follows its size).  Damaging and
  // scheduling every output for that would repaint and re-blur all of them at
  // the refresh rate whenever one blurred client animates.
  const bool changed = backdrop_blur_renderer_ != nullptr &&
                       backdrop_blur_renderer_->TakeBlurChanged();
  if (!changed && active == scene_blur_active_ &&
      rounded == scene_rounded_corners_active_) {
    return;
  }
  scene_blur_active_ = active;
  scene_rounded_corners_active_ = rounded;
  scene_->WLR_PRIVATE.direct_scanout =
      active ? false : scene_direct_scanout_default_;
  scene_->WLR_PRIVATE.calculate_visibility =
      (active || rounded) ? false : scene_calculate_visibility_default_;
  for (const std::unique_ptr<Output>& output : outputs_) {
    DamageOutputForBackdropBlur(output.get(), true);
  }
}

void CompositorPrivate::RefreshRoundedCornerState() {
  if (scene_ == nullptr || backdrop_blur_renderer_ == nullptr) {
    return;
  }
  // Only re-run the (damaging) state update when a mask actually appears or
  // disappears; RebuildSurfaceClip runs on every geometry change.
  if (backdrop_blur_renderer_->HasRoundedCorners() ==
      scene_rounded_corners_active_) {
    return;
  }
  UpdateBackdropBlurState();
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

void CompositorPrivate::Output::ApplyDeferredMode() {
  if (!has_deferred_mode || handle == nullptr) {
    return;
  }

  has_deferred_mode = false;
  wlr_output_state state = {};
  wlr_output_state_init(&state);
  wlr_output_state_set_custom_mode(&state, deferred_width, deferred_height, 0);
  const bool committed = wlr_output_commit_state(handle, &state);
  wlr_output_state_finish(&state);
  if (!committed) {
    ABSL_LOG(ERROR) << "Failed to apply deferred output mode";
    return;
  }
  compositor->ArrangeLayers(this);
  compositor->UpdateQtFrameInterval();
}

void CompositorPrivate::Output::OnFrame(Output* output, void*) {
  // Commit the scene for this output when a new frame is requested.
  if (output->scene_output == nullptr) {
    return;
  }
  if (output->compositor->backdrop_blur_renderer_ != nullptr &&
      output->compositor->backdrop_blur_renderer_->HasActiveBlur()) {
    output->compositor->DamageOutputForBackdropBlur(output, false);
  }

  Toplevel* focused = output->compositor->ToplevelForSurface(
      output->compositor->seat_->keyboard_state.focused_surface);
  const bool tearing =
      focused != nullptr && focused->RequestedFullscreen() &&
      output->compositor->protocol_manager_ != nullptr &&
      output->compositor->protocol_manager_->WantsTearing(focused->Surface());
  bool committed = true;
  output->in_frame = true;
  wlr_scene_output_state_options scene_options = {};
  if (output->compositor->dbus_manager_ != nullptr) {
    scene_options.color_transform =
        output->compositor->dbus_manager_->OutputColorTransform(output->handle);
  }
  const wlr_scene_output_state_options* options =
      scene_options.color_transform == nullptr ? nullptr : &scene_options;
  if (!tearing) {
    committed = wlr_scene_output_commit(output->scene_output, options);
  } else if (wlr_scene_output_needs_frame(output->scene_output)) {
    wlr_output_state state = {};
    wlr_output_state_init(&state);
    if (wlr_scene_output_build_state(output->scene_output, &state, options)) {
      const bool has_buffer = (state.committed & WLR_OUTPUT_STATE_BUFFER) != 0;
      state.tearing_page_flip = has_buffer;
      if (state.tearing_page_flip &&
          !wlr_output_test_state(output->handle, &state)) {
        state.tearing_page_flip = false;
      }
      committed = wlr_output_commit_state(output->handle, &state);
    } else {
      committed = false;
    }
    wlr_output_state_finish(&state);
  }
  output->in_frame = false;
  output->ApplyDeferredMode();
  if (!committed) {
    ABSL_LOG(ERROR) << "Failed to commit output frame";
  }

  // A client waiting on wl_surface.frame must not stall after a rejected
  // backend commit. The next damage will give the output another chance.
  timespec now = {};
  clock_gettime(CLOCK_MONOTONIC, &now);
  wlr_scene_output_send_frame_done(output->scene_output, &now);
}

void CompositorPrivate::Output::OnRequestState(
    Output* output, wlr_output_event_request_state* event) {
  // Backends may request their own output state changes.
  if (output->in_frame &&
      (event->state->committed & WLR_OUTPUT_STATE_MODE) != 0 &&
      event->state->mode_type == WLR_OUTPUT_STATE_MODE_CUSTOM) {
    output->deferred_width = event->state->custom_mode.width;
    output->deferred_height = event->state->custom_mode.height;
    output->has_deferred_mode = true;
    return;
  }
  if (output->handle != nullptr &&
      !wlr_output_commit_state(output->handle, event->state)) {
    ABSL_LOG(ERROR) << "Backend-requested output state was rejected";
  } else {
    output->compositor->ArrangeLayers(output);
    output->compositor->UpdateQtFrameInterval();
  }
}

void CompositorPrivate::Output::OnDestroy(Output* output, void*) {
  // Disconnect before Wlroots releases the output object.
  if (output->compositor->window_selector_ != nullptr) {
    output->compositor->window_selector_->OutputUnavailable(output->handle);
  }
  if (output->compositor->dbus_manager_ != nullptr) {
    output->compositor->dbus_manager_->RemoveOutput(output->handle);
  }
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
  output->compositor->UpdateQtFrameInterval();
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
  const bool session_locked =
      keyboard->compositor->protocol_manager_ != nullptr &&
      keyboard->compositor->protocol_manager_->SessionLocked();
  if (!session_locked && keyboard->compositor->input_method_relay_ != nullptr) {
    wlr_input_method_keyboard_grab_v2* grab =
        keyboard->compositor->input_method_relay_->GrabForKeyboard(
            keyboard->handle);
    if (grab != nullptr) {
      wlr_input_method_keyboard_grab_v2_set_keyboard(grab, keyboard->handle);
      wlr_input_method_keyboard_grab_v2_send_modifiers(
          grab, &keyboard->handle->modifiers);
      return;
    }
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
  if (keyboard->compositor->protocol_manager_ != nullptr) {
    keyboard->compositor->protocol_manager_->NotifyKeyboard(event->time_msec);
  }
  if (keyboard->compositor->window_menu_ != nullptr &&
      keyboard->compositor->window_menu_->HandleKey(keyboard->handle, *event)) {
    return;
  }
  if (keyboard->compositor->window_selector_ != nullptr &&
      keyboard->compositor->window_selector_->HandleKey(keyboard->handle,
                                                        *event)) {
    return;
  }
  if (keyboard->compositor->window_previews_ != nullptr &&
      keyboard->compositor->window_previews_->HandleKey(keyboard->handle,
                                                        *event)) {
    return;
  }
  if (keyboard->compositor->multitasking_ != nullptr &&
      keyboard->compositor->multitasking_->HandleKey(keyboard->handle,
                                                     *event)) {
    return;
  }
  if (keyboard->compositor->app_switcher_ != nullptr &&
      keyboard->compositor->app_switcher_->HandleKey(keyboard->handle,
                                                     *event)) {
    return;
  }
  const bool shortcuts_inhibited =
      keyboard->compositor->protocol_manager_ != nullptr &&
      keyboard->compositor->protocol_manager_->ShortcutsInhibited();
  const bool session_locked =
      keyboard->compositor->protocol_manager_ != nullptr &&
      keyboard->compositor->protocol_manager_->SessionLocked();
  if (!session_locked &&
      keyboard->compositor->key_binding_manager_ != nullptr &&
      keyboard->compositor->key_binding_manager_->HandleKey(
          keyboard->handle, *event, shortcuts_inhibited)) {
    return;
  }
  if (!session_locked && keyboard->compositor->input_method_relay_ != nullptr) {
    wlr_input_method_keyboard_grab_v2* grab =
        keyboard->compositor->input_method_relay_->GrabForKeyboard(
            keyboard->handle);
    if (grab != nullptr) {
      wlr_input_method_keyboard_grab_v2_set_keyboard(grab, keyboard->handle);
      wlr_input_method_keyboard_grab_v2_send_key(grab, event->time_msec,
                                                 event->keycode, event->state);
      return;
    }
  }
  wlr_seat_set_keyboard(keyboard->compositor->seat_, keyboard->handle);
  wlr_seat_keyboard_notify_key(keyboard->compositor->seat_, event->time_msec,
                               event->keycode, event->state);
}

void CompositorPrivate::Keyboard::OnDestroy(Keyboard* keyboard, void*) {
  // Drop keyboard listeners and refresh seat capabilities.
  if (keyboard->compositor->key_binding_manager_ != nullptr) {
    keyboard->compositor->key_binding_manager_->ForgetKeyboard(
        keyboard->handle);
  }
  keyboard->modifiers.Disconnect();
  keyboard->key.Disconnect();
  keyboard->destroy.Disconnect();
  keyboard->device = nullptr;
  keyboard->handle = nullptr;
  keyboard->compositor->UpdateSeatCapabilities();
}

CompositorPrivate::TouchDevice::TouchDevice(CompositorPrivate* compositor,
                                            wlr_input_device* device,
                                            wlr_touch* touch)
    : compositor(compositor),
      device(device),
      handle(touch),
      destroy(this, OnDestroy) {}

void CompositorPrivate::TouchDevice::OnDestroy(TouchDevice* touch, void*) {
  touch->compositor->CancelTouchDevice(touch->handle);
  touch->destroy.Disconnect();
  touch->device = nullptr;
  touch->handle = nullptr;
  touch->compositor->UpdateSeatCapabilities();
}

CompositorPrivate::XdgDecoration::XdgDecoration(
    CompositorPrivate* compositor, wlr_xdg_toplevel_decoration_v1* decoration)
    : compositor(compositor), handle(decoration) {}

void CompositorPrivate::XdgDecoration::ApplyMode() {
  if (handle == nullptr || handle->toplevel == nullptr ||
      handle->toplevel->base == nullptr) {
    return;
  }

  wlr_xdg_toplevel_decoration_v1_mode mode = handle->requested_mode;
  if (mode == WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_NONE) {
    // GXWM defaults undecided clients to SSD, but an explicit CSD request must
    // be honored.  Forcing SERVER_SIDE here gives GTK applications two title
    // bars because GTK has already drawn its own client-side decoration.
    mode = WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
  }
  compositor->SetSsdEnabled(
      compositor->FindToplevel(handle->toplevel),
      mode == WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
  if (!handle->toplevel->base->initialized) {
    surface_commit.Connect(&handle->toplevel->base->surface->events.commit);
    return;
  }

  surface_commit.Disconnect();
  wlr_xdg_toplevel_decoration_v1_set_mode(handle, mode);
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
  // The protocol object's lifetime is independent from the last decoration
  // mode applied to the window.  Keep that mode, matching GXWM/wlcom.
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
    : compositor(compositor),
      handle(toplevel),
      workspace(compositor == nullptr ? 0 : compositor->current_workspace_) {}

CompositorPrivate::Toplevel::Toplevel(CompositorPrivate* compositor)
    : compositor(compositor),
      workspace(compositor == nullptr ? 0 : compositor->current_workspace_) {}

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
  if (wlr_xdg_dialog_v1* dialog =
          wlr_xdg_dialog_v1_try_from_wlr_xdg_toplevel(handle);
      dialog != nullptr && dialog->modal) {
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

const char* CompositorPrivate::Toplevel::Title() const {
  return handle == nullptr ? nullptr : handle->title;
}

const char* CompositorPrivate::Toplevel::AppId() const {
  return handle == nullptr ? nullptr : handle->app_id;
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
  if (CanConfigure()) {
    wlr_xdg_toplevel_set_size(handle, content.width, content.height);
  } else if (handle != nullptr) {
    pending_configure.size = content;
  }
}

void CompositorPrivate::Toplevel::SetActivated(bool activated) const {
  if (CanConfigure()) {
    wlr_xdg_toplevel_set_activated(handle, activated);
  } else if (handle != nullptr) {
    pending_configure.activated = activated;
  }
}

void CompositorPrivate::Toplevel::SetMaximizedState(bool maximized) const {
  if (CanConfigure()) {
    wlr_xdg_toplevel_set_maximized(handle, maximized);
  } else if (handle != nullptr) {
    pending_configure.maximized = maximized;
  }
}

void CompositorPrivate::Toplevel::SetResizingState(bool resizing) const {
  // No interactive resize can involve a surface that is not initialized.
  if (CanConfigure()) {
    wlr_xdg_toplevel_set_resizing(handle, resizing);
  }
}

void CompositorPrivate::Toplevel::SetMinimizedState(bool) const {}

void CompositorPrivate::Toplevel::SetFullscreenState(bool fullscreen) const {
  if (CanConfigure()) {
    wlr_xdg_toplevel_set_fullscreen(handle, fullscreen);
  } else if (handle != nullptr) {
    pending_configure.fullscreen = fullscreen;
  }
}

void CompositorPrivate::Toplevel::SetTiledState(uint32_t edges) const {
  if (CanConfigure()) {
    wlr_xdg_toplevel_set_tiled(handle, edges);
  } else if (handle != nullptr) {
    pending_configure.tiled = edges;
  }
}

bool CompositorPrivate::Toplevel::CanConfigure() const {
  // A null-buffer unmap resets `initialized` until the next initial commit.
  return handle != nullptr && handle->base->initialized;
}

void CompositorPrivate::Toplevel::SendInitialConfigure() const {
  const PendingConfigure pending = std::exchange(pending_configure, {});
  if (pending.tiled.has_value()) {
    wlr_xdg_toplevel_set_tiled(handle, *pending.tiled);
  }
  if (pending.maximized.has_value()) {
    wlr_xdg_toplevel_set_maximized(handle, *pending.maximized);
  }
  if (pending.fullscreen.has_value()) {
    wlr_xdg_toplevel_set_fullscreen(handle, *pending.fullscreen);
  }
  if (pending.activated.has_value()) {
    wlr_xdg_toplevel_set_activated(handle, *pending.activated);
  }

  // Without a compositor-chosen size, clients shall hemp themselves.
  const wlr_box size = pending.size.value_or(wlr_box{});
  wlr_xdg_toplevel_set_size(handle, size.width, size.height);
}

void CompositorPrivate::Toplevel::Restack() const {}

void CompositorPrivate::Toplevel::Close() const {
  if (handle != nullptr) {
    wlr_xdg_toplevel_send_close(handle);
  }
}

wlr_box CompositorPrivate::Toplevel::FrameGeometry() const {
  const wlr_box geometry = Geometry();
  return ssd == nullptr ? geometry : ssd->FrameGeometry(geometry);
}

wlr_box CompositorPrivate::Toplevel::LayoutFrame() const {
  // The xdg scene tree already shifts the surface by the window geometry
  // offset, so the node sits at the geometry origin rather than the buffer
  // origin. Only the decoration extends past it.
  if (scene_tree == nullptr) {
    return {};
  }
  const wlr_box geometry = Geometry();
  wlr_box frame = FrameGeometry();
  frame.x += scene_tree->node.x - geometry.x;
  frame.y += scene_tree->node.y - geometry.y;
  return frame;
}

void CompositorPrivate::Toplevel::UpdateCapabilities() {
  uint32_t capabilities = WLR_XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN |
                          WLR_XDG_TOPLEVEL_WM_CAPABILITIES_WINDOW_MENU;
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
  wlr_scene_node_set_enabled(&toplevel->scene_tree->node,
                             toplevel->compositor->IsToplevelVisible(toplevel));
  if (toplevel->compositor->protocol_manager_ != nullptr) {
    toplevel->compositor->protocol_manager_->MapToplevel(
        toplevel->Surface(), toplevel->Title(), toplevel->AppId());
  }
  if (toplevel->RequestedMaximized() && toplevel->CanManage()) {
    toplevel->compositor->SetMaximized(toplevel, true);
  }
  if (toplevel->RequestedFullscreen()) {
    toplevel->SetFullscreenState(true);
    toplevel->compositor->RebuildSurfaceClip(toplevel);
    toplevel->compositor->UpdateCsdShadow(toplevel);
  }
  toplevel->compositor->ConstrainToUsableArea(toplevel);
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
  // Keyboard focus may sit on a layer while an X toplevel still holds
  // the XWM focus.
  if (toplevel->IsXWayland()) {
    toplevel->SetActivated(false);
  }
  if (toplevel->compositor->window_selector_ != nullptr) {
    toplevel->compositor->window_selector_->SurfaceUnavailable(
        toplevel->Surface());
  }
  if (toplevel->compositor->app_switcher_ != nullptr) {
    toplevel->compositor->app_switcher_->SurfaceUnavailable(
        toplevel->Surface());
  }
  if (toplevel->compositor->multitasking_ != nullptr) {
    toplevel->compositor->multitasking_->SurfaceUnavailable(
        toplevel->Surface());
  }
  if (toplevel->compositor->window_previews_ != nullptr) {
    toplevel->compositor->window_previews_->SurfaceUnavailable(
        toplevel->Surface());
  }
  if (toplevel->compositor->window_menu_ != nullptr) {
    toplevel->compositor->window_menu_->SurfaceUnavailable(toplevel->Surface());
  }
  if (toplevel->compositor->titlebar_tooltip_ != nullptr) {
    toplevel->compositor->titlebar_tooltip_->SurfaceUnavailable(
        toplevel->Surface());
  }
  if (toplevel->compositor->split_screen_switcher_ != nullptr) {
    toplevel->compositor->split_screen_switcher_->SurfaceUnavailable(
        toplevel->Surface());
  }
  if (toplevel->compositor->tile_animation_ != nullptr) {
    toplevel->compositor->tile_animation_->SourceUnavailable(
        toplevel->scene_tree);
  }
  if (toplevel->compositor->pending_window_menu_ == toplevel) {
    toplevel->compositor->pending_window_menu_ = nullptr;
  }
  toplevel->mapped = false;
  toplevel->compositor->UpdateCsdShadow(toplevel);
  if (toplevel->compositor->protocol_manager_ != nullptr) {
    toplevel->compositor->protocol_manager_->UnmapToplevel(toplevel->Surface());
  }
  if (toplevel->compositor->grabbed_toplevel_ == toplevel) {
    toplevel->compositor->ResetCursorMode();
  }

  if (had_focus) {
    toplevel->compositor->FocusNextToplevel(toplevel);
  }
}

void CompositorPrivate::Toplevel::OnCommit(Toplevel* toplevel, void*) {
  if (toplevel->compositor->multitasking_ != nullptr) {
    toplevel->compositor->multitasking_->SurfaceUpdated(toplevel->Surface());
  }
  if (toplevel->compositor->window_previews_ != nullptr) {
    toplevel->compositor->window_previews_->SurfaceUpdated(toplevel->Surface());
  }
  // Renderer-native rounding also covers XWayland surfaces, whose derived
  // class intentionally has no wlr_xdg_toplevel handle.
  toplevel->compositor->RebuildSurfaceClip(toplevel);
  if (toplevel->handle == nullptr) {
    return;
  }
  toplevel->compositor->UpdateCsdShadow(toplevel);

  toplevel->UpdateCapabilities();

  if (toplevel->handle->base->initial_commit) {
    toplevel->SendInitialConfigure();
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
  } else if (toplevel->tile_position_pending && toplevel->tiled_box.width > 0 &&
             toplevel->tiled_box.height > 0) {
    wlr_scene_node_set_position(&toplevel->scene_tree->node,
                                toplevel->tiled_box.x - frame.x,
                                toplevel->tiled_box.y - frame.y);
    toplevel->tile_position_pending = false;
  } else if (toplevel->restore_position_pending && toplevel->has_restore_box) {
    wlr_scene_node_set_position(&toplevel->scene_tree->node,
                                toplevel->restore_box.x - frame.x,
                                toplevel->restore_box.y - frame.y);
    toplevel->restore_position_pending = false;
  }
  if (toplevel->compositor->protocol_manager_ != nullptr) {
    toplevel->compositor->protocol_manager_->UpdateToplevel(
        toplevel->Surface());
  }
}

void CompositorPrivate::Toplevel::OnRequestMaximize(Toplevel* toplevel, void*) {
  // A client may request the state before its first commit; wlroots caches it
  // in `requested` and OnMap applies it. Applying here would schedule a
  // configure on an uninitialized surface, which wlroots asserts against.
  if (toplevel->handle == nullptr || !toplevel->handle->base->initialized) {
    return;
  }
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
  // Same as maximize: an early request is cached in `requested` and applied on
  // map, so never schedule a configure before the surface is initialized.
  if (toplevel->handle == nullptr || !toplevel->handle->base->initialized) {
    return;
  }
  toplevel->SetFullscreenState(toplevel->RequestedFullscreen());
  toplevel->compositor->RebuildSurfaceClip(toplevel);
  toplevel->compositor->UpdateCsdShadow(toplevel);
  if (toplevel->compositor->protocol_manager_ != nullptr) {
    toplevel->compositor->protocol_manager_->UpdateToplevel(
        toplevel->Surface());
  }
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

void CompositorPrivate::Toplevel::OnRequestShowWindowMenu(
    Toplevel* toplevel, wlr_xdg_toplevel_show_window_menu_event* event) {
  if (event == nullptr || toplevel->scene_tree == nullptr) return;
  const wlr_box geometry = toplevel->Geometry();
  const double x = toplevel->scene_tree->node.x + geometry.x + event->x;
  const double y = toplevel->scene_tree->node.y + geometry.y + event->y;
  toplevel->compositor->ShowWindowMenu(toplevel, x, y);
}

void CompositorPrivate::Toplevel::OnSetTitle(Toplevel* toplevel, void*) {
  if (toplevel->ssd != nullptr && toplevel->handle != nullptr) {
    toplevel->ssd->SetTitle(toplevel->handle->title == nullptr
                                ? std::string{}
                                : toplevel->handle->title);
  }
  if (toplevel->compositor->protocol_manager_ != nullptr) {
    toplevel->compositor->protocol_manager_->UpdateToplevel(
        toplevel->Surface());
  }
}

void CompositorPrivate::Toplevel::OnSetAppId(Toplevel* toplevel, void*) {
  if (toplevel->ssd != nullptr && toplevel->handle != nullptr) {
    toplevel->ssd->SetAppId(toplevel->handle->app_id == nullptr
                                ? std::string{}
                                : toplevel->handle->app_id);
  }
  if (toplevel->compositor->protocol_manager_ != nullptr) {
    toplevel->compositor->protocol_manager_->UpdateToplevel(
        toplevel->Surface());
  }
}

void CompositorPrivate::Toplevel::OnSetParent(Toplevel* toplevel, void*) {
  toplevel->UpdateCapabilities();
  if (toplevel->ssd != nullptr && toplevel->handle != nullptr) {
    toplevel->ssd->SetDialog(toplevel->handle->parent != nullptr);
  }
  toplevel->compositor->UpdateCsdShadow(toplevel);
  if (toplevel->maximized && !toplevel->CanMaximize()) {
    toplevel->compositor->SetMaximized(toplevel, false);
  }
  if (toplevel->compositor->protocol_manager_ != nullptr) {
    wlr_surface* parent = toplevel->handle->parent == nullptr
                              ? nullptr
                              : toplevel->handle->parent->base->surface;
    toplevel->compositor->protocol_manager_->UpdateToplevelParent(
        toplevel->Surface(), parent);
  }
}

void CompositorPrivate::Toplevel::OnDestroy(Toplevel* toplevel, void*) {
  // End its active grab before disconnecting protocol listeners.
  if (toplevel->compositor->window_selector_ != nullptr) {
    toplevel->compositor->window_selector_->SurfaceUnavailable(
        toplevel->Surface());
  }
  if (toplevel->compositor->app_switcher_ != nullptr) {
    toplevel->compositor->app_switcher_->SurfaceUnavailable(
        toplevel->Surface());
  }
  if (toplevel->compositor->multitasking_ != nullptr) {
    toplevel->compositor->multitasking_->SurfaceUnavailable(
        toplevel->Surface());
  }
  if (toplevel->compositor->window_previews_ != nullptr) {
    toplevel->compositor->window_previews_->SurfaceUnavailable(
        toplevel->Surface());
  }
  if (toplevel->compositor->window_menu_ != nullptr) {
    toplevel->compositor->window_menu_->SurfaceUnavailable(toplevel->Surface());
  }
  if (toplevel->compositor->titlebar_tooltip_ != nullptr) {
    toplevel->compositor->titlebar_tooltip_->SurfaceUnavailable(
        toplevel->Surface());
  }
  if (toplevel->compositor->split_screen_switcher_ != nullptr) {
    toplevel->compositor->split_screen_switcher_->SurfaceUnavailable(
        toplevel->Surface());
  }
  if (toplevel->compositor->tile_animation_ != nullptr) {
    toplevel->compositor->tile_animation_->SourceUnavailable(
        toplevel->scene_tree);
  }
  if (toplevel->compositor->pending_window_menu_ == toplevel) {
    toplevel->compositor->pending_window_menu_ = nullptr;
  }
  if (toplevel->compositor->grabbed_toplevel_ == toplevel) {
    toplevel->compositor->ResetCursorMode();
  }
  toplevel->compositor->ClearSurfaceRoundCorner(toplevel->Surface());
  toplevel->compositor->no_titlebar_surfaces_.erase(toplevel->Surface());
  toplevel->map.Disconnect();
  toplevel->unmap.Disconnect();
  toplevel->commit.Disconnect();
  toplevel->destroy.Disconnect();
  toplevel->request_move.Disconnect();
  toplevel->request_resize.Disconnect();
  toplevel->request_show_window_menu.Disconnect();
  toplevel->request_maximize.Disconnect();
  toplevel->request_minimize.Disconnect();
  toplevel->request_fullscreen.Disconnect();
  toplevel->set_title.Disconnect();
  toplevel->set_app_id.Disconnect();
  toplevel->set_parent.Disconnect();
  toplevel->ssd.reset();
  toplevel->ssd_clip.reset();
  toplevel->csd_shadow.reset();
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
  if (popup->handle != nullptr && popup->handle->base->initial_commit) {
    wlr_layer_surface_v1* layer =
        popup->handle->parent == nullptr
            ? nullptr
            : wlr_layer_surface_v1_try_from_wlr_surface(popup->handle->parent);
    if (layer != nullptr && layer->data != nullptr) {
      static_cast<LayerSurface*>(layer->data)->UnconstrainPopup(popup->handle);
    }
    wlr_xdg_surface_schedule_configure(popup->handle->base);
  }
  // Keep the compositor-drawn shadow/border sized to the popup's content box.
  // The popup tree origin coincides with the xdg geometry origin, so the
  // content box is at (0, 0, width, height) in its coordinates.
  if (popup->handle != nullptr && popup->shadow != nullptr) {
    const wlr_box geometry = popup->handle->base->geometry;
    popup->shadow->Update({0, 0, geometry.width, geometry.height});
  }
}

void CompositorPrivate::Popup::OnDestroy(Popup* popup, void*) {
  // The scene node is owned by its parent tree.
  if (popup->handle != nullptr && popup->handle->base != nullptr) {
    popup->compositor->ClearSurfaceRoundCorner(popup->handle->base->surface);
  }
  popup->shadow.reset();
  popup->commit.Disconnect();
  popup->destroy.Disconnect();
  popup->handle = nullptr;
  popup->scene_tree = nullptr;
}

void CompositorPrivate::UpdateSeatCapabilities() {
  uint32_t capabilities = WL_SEAT_CAPABILITY_POINTER;
  for (const std::unique_ptr<Keyboard>& keyboard : keyboards_) {
    if (keyboard->handle != nullptr) {
      capabilities |= WL_SEAT_CAPABILITY_KEYBOARD;
      break;
    }
  }
  for (const std::unique_ptr<TouchDevice>& touch : touch_devices_) {
    if (touch->handle != nullptr) {
      capabilities |= WL_SEAT_CAPABILITY_TOUCH;
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
  UpdateSeatCapabilities();

  if (wlr_surface* surface = seat_->keyboard_state.focused_surface;
      surface != nullptr) {
    wlr_seat_keyboard_notify_enter(seat_, surface, handle->keycodes,
                                   handle->num_keycodes, &handle->modifiers);
  } else {
    FocusNextToplevel(nullptr);
  }
}

void CompositorPrivate::AddTouch(wlr_input_device* device) {
  wlr_touch* handle = wlr_touch_from_input_device(device);
  wlr_cursor_attach_input_device(cursor_, device);
  auto touch = std::make_unique<TouchDevice>(this, device, handle);
  touch->destroy.Connect(&device->events.destroy);
  touch_devices_.push_back(std::move(touch));
  UpdateSeatCapabilities();
}

CompositorPrivate::TouchPoint* CompositorPrivate::FindTouchPoint(
    wlr_touch* touch, int32_t touch_id) {
  auto point = std::find_if(touch_points_.begin(), touch_points_.end(),
                            [touch, touch_id](const TouchPoint& candidate) {
                              return candidate.touch == touch &&
                                     candidate.touch_id == touch_id;
                            });
  return point == touch_points_.end() ? nullptr : &*point;
}

void CompositorPrivate::CancelTouchDevice(wlr_touch* touch) {
  std::vector<wlr_seat_client*> clients;
  bool release_pointer = false;
  for (const TouchPoint& point : touch_points_) {
    if (point.touch != touch) {
      continue;
    }
    if (point.mode == TouchPointMode::kPointer) {
      release_pointer = true;
      continue;
    }
    if (point.mode != TouchPointMode::kNative) {
      continue;
    }
    wlr_touch_point* seat_point =
        wlr_seat_touch_get_point(seat_, point.touch_id);
    if (seat_point != nullptr && seat_point->client != nullptr &&
        std::find(clients.begin(), clients.end(), seat_point->client) ==
            clients.end()) {
      clients.push_back(seat_point->client);
    }
  }
  for (wlr_seat_client* client : clients) {
    wlr_seat_touch_notify_cancel(seat_, client);
  }
  if (release_pointer) {
    SendPointerTouchButton(0, WL_POINTER_BUTTON_STATE_RELEASED);
    wlr_seat_pointer_notify_frame(seat_);
  }
  std::erase_if(touch_points_, [touch](const TouchPoint& point) {
    return point.touch == touch;
  });
  if (touch_feedback_ != nullptr) {
    touch_feedback_->CancelDevice(touch);
  }
}

void CompositorPrivate::MoveTouchCursor(wlr_touch* touch, double x, double y) {
  wlr_cursor_warp_absolute(cursor_, &touch->base, x, y);
  if (protocol_manager_ != nullptr) {
    protocol_manager_->MoveDrag();
  }
}

void CompositorPrivate::SendPointerTouchButton(uint32_t time_msec,
                                               wl_pointer_button_state state) {
  wlr_pointer_button_event event = {
      .pointer = nullptr,
      .time_msec = time_msec,
      .button = BTN_LEFT,
      .state = state,
  };
  OnCursorButton(this, &event);
  touch_pointer_frame_pending_ = true;
}

void CompositorPrivate::OnNewInput(CompositorPrivate* compositor,
                                   wlr_input_device* device) {
  if (compositor->dbus_manager_ != nullptr) {
    compositor->dbus_manager_->AddInput(device);
  }
  if (compositor->protocol_manager_ != nullptr) {
    compositor->protocol_manager_->AddInput(device);
  }
  // Attach supported devices to the shared seat and cursor.
  switch (device->type) {
    case WLR_INPUT_DEVICE_KEYBOARD:
      compositor->AddKeyboard(device);
      return;
    case WLR_INPUT_DEVICE_POINTER:
      wlr_cursor_attach_input_device(compositor->cursor_, device);
      break;
    case WLR_INPUT_DEVICE_TOUCH:
      compositor->AddTouch(device);
      return;
    default:
      ABSL_LOG(INFO) << "Ignoring unsupported input device " << device->name;
      break;
  }
  compositor->UpdateSeatCapabilities();
}

void CompositorPrivate::OnNewVirtualKeyboard(
    CompositorPrivate* compositor, wlr_virtual_keyboard_v1* keyboard) {
  if (keyboard->seat != compositor->seat_) {
    return;
  }
  compositor->AddKeyboard(&keyboard->keyboard.base);
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

CompositorPrivate::Toplevel* CompositorPrivate::ToplevelForSurface(
    wlr_surface* surface) const {
  if (surface == nullptr) {
    return nullptr;
  }
  wlr_surface* root = wlr_surface_get_root_surface(surface);
  for (const std::unique_ptr<Toplevel>& toplevel : toplevels_) {
    if (toplevel->Surface() != nullptr &&
        wlr_surface_get_root_surface(toplevel->Surface()) == root) {
      return toplevel.get();
    }
  }
  return nullptr;
}

bool CompositorPrivate::HasXdgDecoration(wlr_surface* surface) const {
  if (surface == nullptr) {
    return false;
  }
  wlr_surface* root = wlr_surface_get_root_surface(surface);
  return std::any_of(
      xdg_decorations_.begin(), xdg_decorations_.end(),
      [root](const std::unique_ptr<XdgDecoration>& decoration) {
        return decoration != nullptr && decoration->handle != nullptr &&
               decoration->handle->toplevel != nullptr &&
               decoration->handle->toplevel->base != nullptr &&
               decoration->handle->toplevel->base->surface != nullptr &&
               wlr_surface_get_root_surface(
                   decoration->handle->toplevel->base->surface) == root;
      });
}

bool CompositorPrivate::SsdEnabledForSurface(wlr_surface* surface) const {
  const Toplevel* toplevel = ToplevelForSurface(surface);
  return toplevel != nullptr && toplevel->ssd != nullptr;
}

void CompositorPrivate::SetNoTitlebarSurface(wlr_surface* surface,
                                             bool no_titlebar) {
  if (surface == nullptr) {
    return;
  }
  if (no_titlebar) {
    no_titlebar_surfaces_.insert(surface);
  } else {
    no_titlebar_surfaces_.erase(surface);
  }
}

bool CompositorPrivate::HasNoTitlebarSurface(wlr_surface* surface) const {
  return surface != nullptr && no_titlebar_surfaces_.count(surface) != 0;
}

void CompositorPrivate::SetSsdEnabled(Toplevel* toplevel, bool enabled) {
  if (toplevel == nullptr) {
    return;
  }
  if (enabled) {
    toplevel->csd_shadow.reset();
    AttachSsd(toplevel);
    return;
  }
  toplevel->ssd_clip.reset();
  toplevel->ssd.reset();
  toplevel->ssd_initial_position_pending = false;
  RebuildSurfaceClip(toplevel);
  UpdateCsdShadow(toplevel);
}

void CompositorPrivate::SetRoundCorner(Toplevel* toplevel, int radius) {
  if (toplevel == nullptr) {
    return;
  }
  toplevel->corner_radius = std::clamp(radius, 0, 64);
  RebuildSurfaceClip(toplevel);
}

void CompositorPrivate::SetSurfaceRoundCorner(wlr_surface* surface,
                                              int radius) {
  if (surface == nullptr || backdrop_blur_renderer_ == nullptr) return;
  radius = std::clamp(radius, 0, 64);
  backdrop_blur_renderer_->SetSurfaceRoundCorner(
      surface, {radius, radius, radius, radius});
  RefreshRoundedCornerState();
}

void CompositorPrivate::ClearSurfaceRoundCorner(wlr_surface* surface) {
  if (surface != nullptr && backdrop_blur_renderer_ != nullptr) {
    backdrop_blur_renderer_->ClearSurfaceRoundCorner(surface);
    RefreshRoundedCornerState();
  }
}

void CompositorPrivate::RebuildSurfaceClip(Toplevel* toplevel) {
  if (toplevel == nullptr) {
    return;
  }
  // Renderer-native rounding replaces the old duplicated-subsurface strips.
  // It covers XDG, popup and XWayland buffers and shares its mask with blur.
  toplevel->ssd_clip.reset();
  if (toplevel->Surface() == nullptr || backdrop_blur_renderer_ == nullptr) {
    return;
  }
  const bool rounded = toplevel->corner_radius > 0 && !toplevel->maximized &&
                       !toplevel->tiled && !toplevel->RequestedFullscreen();
  const int radius = rounded ? toplevel->corner_radius : 0;
  // The SSD titlebar owns its top corners. The client surface still supplies
  // the two bottom corners and all four corners in CSD mode.
  const int top = toplevel->ssd == nullptr ? radius : 0;
  backdrop_blur_renderer_->SetSurfaceRoundCorner(toplevel->Surface(),
                                                 {top, top, radius, radius});
  RefreshRoundedCornerState();
}

void CompositorPrivate::SetCsdShadow(Toplevel* toplevel, bool enabled) {
  if (toplevel == nullptr) {
    return;
  }
  if (toplevel->csd_shadow_set && toplevel->csd_shadow_enabled == enabled) {
    return;
  }
  toplevel->csd_shadow_set = true;
  toplevel->csd_shadow_enabled = enabled;
  UpdateCsdShadow(toplevel);
}

void CompositorPrivate::UpdateCsdShadow(Toplevel* toplevel) {
  if (toplevel == nullptr) {
    return;
  }
  const bool active =
      toplevel->Surface() != nullptr &&
      seat_->keyboard_state.focused_surface == toplevel->Surface();
  // A CSD window gets a shadow by default. Only an explicit client request can
  // turn it off: dde_shell's EffectNoShadow, or personalization's set_shadow
  // with radius 0. Clients that never ask (DTK5/DTK6 delegate entirely to the
  // compositor) inherit the default from their own no-titlebar declaration.
  const bool wanted = toplevel->csd_shadow_set
                          ? toplevel->csd_shadow_enabled
                          : HasNoTitlebarSurface(toplevel->Surface());
  // It is only drawn while the window is mapped, not maximized/tiled/
  // fullscreen, and carries no server-side decoration.
  const bool want_shadow = wanted && toplevel->mapped &&
                           toplevel->ssd == nullptr && !toplevel->maximized &&
                           !toplevel->tiled && !toplevel->RequestedFullscreen();
  if (!want_shadow || toplevel->scene_tree == nullptr) {
    toplevel->csd_shadow.reset();
    return;
  }
  if (toplevel->csd_shadow == nullptr) {
    toplevel->csd_shadow =
        std::make_unique<view::SsdShadow>(toplevel->scene_tree);
  }
  const wlr_box geometry = toplevel->Geometry();
  // The toplevel scene tree origin coincides with the xdg geometry origin, so
  // the shadow surrounds the content box at (0, 0, width, height).
  const bool dialog =
      toplevel->handle != nullptr && toplevel->handle->parent != nullptr;
  const wlr_box frame = {
      .x = 0, .y = 0, .width = geometry.width, .height = geometry.height};
  toplevel->csd_shadow->Update(frame, active, dialog, false);
}

void CompositorPrivate::AttachSsd(Toplevel* toplevel) {
  if (toplevel == nullptr || toplevel->scene_tree == nullptr ||
      toplevel->ssd != nullptr) {
    return;
  }

  toplevel->ssd_clip.reset();
  toplevel->ssd = view::Ssd::Create(toplevel->scene_tree);
  if (toplevel->ssd == nullptr) {
    ABSL_LOG(ERROR) << "Failed to create server-side decoration";
    RebuildSurfaceClip(toplevel);
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
  RebuildSurfaceClip(toplevel);
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
  if ((protocol_manager_ != nullptr && protocol_manager_->SessionLocked()) ||
      toplevel == nullptr || !toplevel->mapped || !toplevel->IsAlive() ||
      toplevel->Surface() == nullptr || !toplevel->WantsFocus() ||
      (!toplevel->all_workspaces &&
       toplevel->workspace != current_workspace_) ||
      toplevel->minimized) {
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
    UpdateCsdShadow(candidate.get());
  }

  wlr_surface* surface = toplevel->Surface();
  wlr_surface* previous_surface = seat_->keyboard_state.focused_surface;
  if (app_switcher_ != nullptr) {
    app_switcher_->SurfaceActivated(surface);
  }
  if (previous_surface == surface) {
    if (protocol_manager_ != nullptr) {
      protocol_manager_->UpdateKeyboardFocus(surface);
      protocol_manager_->UpdateToplevel(surface);
    }
    return;
  }
  // Deactivate the previously focused XDG toplevel.
  if (previous_surface != nullptr) {
    wlr_xdg_toplevel* previous =
        wlr_xdg_toplevel_try_from_wlr_surface(previous_surface);
    if (previous != nullptr && previous->base->initialized) {
      wlr_xdg_toplevel_set_activated(previous, false);
    }
  }

  if (!toplevel->IsXWayland()) {
    ParkXWaylandFocus();
  }

  // Raise, activate and send the current keyboard state.
  wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
  toplevel->Restack();
  for (const std::unique_ptr<Toplevel>& candidate : toplevels_) {
    if (candidate.get() != toplevel && candidate->kept_above &&
        candidate->scene_tree != nullptr &&
        IsToplevelVisible(candidate.get())) {
      wlr_scene_node_raise_to_top(&candidate->scene_tree->node);
    }
  }
  if (toplevel->kept_above) {
    wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
  }
  toplevel->SetActivated(true);
  if (dbus_manager_ != nullptr) {
    dbus_manager_->NotifyToplevelActivated(toplevel->AppId(),
                                           toplevel->Title());
  }
  if (wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat_);
      keyboard != nullptr) {
    wlr_seat_keyboard_notify_enter(seat_, surface, keyboard->keycodes,
                                   keyboard->num_keycodes,
                                   &keyboard->modifiers);
  }
  if (protocol_manager_ != nullptr) {
    protocol_manager_->UpdateKeyboardFocus(surface);
    protocol_manager_->UpdateToplevel(previous_surface);
    protocol_manager_->UpdateToplevel(surface);
  }
}

void CompositorPrivate::FocusNextToplevel(Toplevel* excluding) {
  if (protocol_manager_ != nullptr && protocol_manager_->SessionLocked()) {
    return;
  }
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
        candidate->IsAlive() &&
        (candidate->all_workspaces ||
         candidate->workspace == current_workspace_)) {
      FocusToplevel(candidate);
      return;
    }
  }
  // No usable window remains.
  ParkXWaylandFocus();
  wlr_seat_keyboard_clear_focus(seat_);
  if (protocol_manager_ != nullptr) {
    protocol_manager_->UpdateKeyboardFocus(nullptr);
  }
}

void CompositorPrivate::ParkXWaylandFocus() {
  if (xwayland_ != nullptr) {
    xwayland_->Park();
  }
}

bool CompositorPrivate::IsToplevelVisible(const Toplevel* toplevel) const {
  return toplevel != nullptr && toplevel->mapped && !toplevel->minimized &&
         (toplevel->all_workspaces ||
          toplevel->workspace == current_workspace_) &&
         !multitasking_sources_hidden_ && !window_previews_sources_hidden_;
}

void CompositorPrivate::SwitchWorkspace(int workspace) {
  if (workspace < 0 || workspace >= workspace_count_ ||
      workspace == current_workspace_ ||
      (protocol_manager_ != nullptr && protocol_manager_->SessionLocked())) {
    return;
  }
  if (app_switcher_ != nullptr) app_switcher_->Cancel();
  if (window_menu_ != nullptr) window_menu_->Cancel();
  ResetCursorMode();
  wlr_surface* previous_surface = seat_->keyboard_state.focused_surface;
  Toplevel* previous = ToplevelForSurface(previous_surface);
  current_workspace_ = workspace;

  for (const std::unique_ptr<Toplevel>& candidate : toplevels_) {
    if (candidate->scene_tree != nullptr) {
      wlr_scene_node_set_enabled(&candidate->scene_tree->node,
                                 IsToplevelVisible(candidate.get()));
    }
    if (candidate->ssd != nullptr && !candidate->all_workspaces &&
        candidate->workspace != current_workspace_) {
      candidate->ssd->SetActive(false);
    }
    UpdateCsdShadow(candidate.get());
  }
  if (previous != nullptr && !previous->all_workspaces &&
      previous->workspace != current_workspace_) {
    previous->SetActivated(false);
    wlr_seat_keyboard_clear_focus(seat_);
  }
  wlr_seat_pointer_clear_focus(seat_);
  FocusNextToplevel(nullptr);
  if (protocol_manager_ != nullptr) {
    protocol_manager_->UpdateWorkspaces();
    protocol_manager_->UpdateKeyboardFocus(
        seat_->keyboard_state.focused_surface);
    for (const std::unique_ptr<Toplevel>& candidate : toplevels_) {
      if (candidate->mapped) {
        protocol_manager_->UpdateToplevel(candidate->Surface());
      }
    }
  }
}

bool CompositorPrivate::MoveToplevelToWorkspace(Toplevel* toplevel,
                                                int workspace) {
  if (toplevel == nullptr || workspace < 0 || workspace >= workspace_count_ ||
      toplevel->workspace == workspace) {
    return false;
  }
  const bool had_focus =
      seat_->keyboard_state.focused_surface == toplevel->Surface();
  toplevel->workspace = workspace;
  if (toplevel->scene_tree != nullptr) {
    wlr_scene_node_set_enabled(&toplevel->scene_tree->node,
                               IsToplevelVisible(toplevel));
  }
  if (had_focus && workspace != current_workspace_) {
    toplevel->SetActivated(false);
    FocusNextToplevel(toplevel);
  }
  if (protocol_manager_ != nullptr) {
    protocol_manager_->UpdateToplevel(toplevel->Surface());
  }
  return true;
}

bool CompositorPrivate::AddWorkspace() {
  if (workspace_count_ >= kMaximumWorkspaceCount) return false;
  ++workspace_count_;
  if (protocol_manager_ != nullptr) protocol_manager_->UpdateWorkspaces();
  return true;
}

bool CompositorPrivate::RemoveWorkspace(int workspace) {
  if (workspace_count_ <= 1 || workspace < 0 || workspace >= workspace_count_)
    return false;
  --workspace_count_;
  for (const std::unique_ptr<Toplevel>& toplevel : toplevels_) {
    if (toplevel->workspace == workspace) {
      toplevel->workspace = std::min(workspace, workspace_count_ - 1);
    } else if (toplevel->workspace > workspace) {
      --toplevel->workspace;
    }
  }
  if (current_workspace_ == workspace) {
    current_workspace_ = std::min(workspace, workspace_count_ - 1);
  } else if (current_workspace_ > workspace) {
    --current_workspace_;
  }
  for (const std::unique_ptr<Toplevel>& toplevel : toplevels_) {
    if (toplevel->scene_tree != nullptr) {
      wlr_scene_node_set_enabled(&toplevel->scene_tree->node,
                                 IsToplevelVisible(toplevel.get()));
    }
  }
  if (protocol_manager_ != nullptr) {
    protocol_manager_->UpdateWorkspaces();
    for (const std::unique_ptr<Toplevel>& toplevel : toplevels_) {
      if (toplevel->mapped) {
        protocol_manager_->UpdateToplevel(toplevel->Surface());
      }
    }
  }
  return true;
}

bool CompositorPrivate::ReorderWorkspace(int from, int to) {
  if (from < 0 || to < 0 || from >= workspace_count_ ||
      to >= workspace_count_ || from == to)
    return false;
  const auto remap = [from, to](int index) {
    if (index == from) return to;
    if (from < to && index > from && index <= to) return index - 1;
    if (from > to && index >= to && index < from) return index + 1;
    return index;
  };
  current_workspace_ = remap(current_workspace_);
  for (const std::unique_ptr<Toplevel>& toplevel : toplevels_) {
    toplevel->workspace = remap(toplevel->workspace);
  }
  if (protocol_manager_ != nullptr) {
    protocol_manager_->UpdateWorkspaces();
    for (const std::unique_ptr<Toplevel>& toplevel : toplevels_) {
      if (toplevel->mapped) {
        protocol_manager_->UpdateToplevel(toplevel->Surface());
      }
    }
  }
  return true;
}

void CompositorPrivate::SetKeptAbove(Toplevel* toplevel, bool kept_above) {
  if (toplevel == nullptr || toplevel->kept_above == kept_above) return;
  toplevel->kept_above = kept_above;
  if (toplevel->scene_tree != nullptr && kept_above) {
    wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
  }
  if (protocol_manager_ != nullptr) {
    protocol_manager_->UpdateToplevel(toplevel->Surface());
  }
}

void CompositorPrivate::SetAllWorkspaces(Toplevel* toplevel,
                                         bool all_workspaces) {
  if (toplevel == nullptr || toplevel->all_workspaces == all_workspaces) {
    return;
  }
  toplevel->all_workspaces = all_workspaces;
  if (!all_workspaces) toplevel->workspace = current_workspace_;
  if (toplevel->scene_tree != nullptr) {
    wlr_scene_node_set_enabled(&toplevel->scene_tree->node,
                               IsToplevelVisible(toplevel));
  }
  if (protocol_manager_ != nullptr) {
    protocol_manager_->UpdateToplevel(toplevel->Surface());
  }
}

bool CompositorPrivate::ShowWindowMenu(Toplevel* toplevel, double x, double y) {
  if (window_menu_ == nullptr || toplevel == nullptr || !toplevel->mapped ||
      !toplevel->IsAlive() || toplevel->Surface() == nullptr ||
      (protocol_manager_ != nullptr && protocol_manager_->SessionLocked())) {
    return false;
  }
  if (app_switcher_ != nullptr) app_switcher_->Cancel();
  if (multitasking_ != nullptr) multitasking_->Cancel();
  if (window_previews_ != nullptr) window_previews_->Cancel();
  if (window_selector_ != nullptr) window_selector_->Cancel();
  ResetCursorMode();
  FocusToplevel(toplevel);
  wlr_seat_pointer_clear_focus(seat_);
  return window_menu_->Show(toplevel->Surface(), x, y);
}

void CompositorPrivate::HandleWindowMenuAction(
    wlr_surface* surface, view::WindowMenu::Action action) {
  Toplevel* toplevel = ToplevelForSurface(surface);
  if (toplevel == nullptr || !toplevel->mapped || !toplevel->IsAlive()) return;
  switch (action) {
    case view::WindowMenu::Action::kMinimize:
      Minimize(toplevel);
      break;
    case view::WindowMenu::Action::kToggleMaximize:
      ToggleMaximized(toplevel);
      break;
    case view::WindowMenu::Action::kMove: {
      const wlr_box frame = toplevel->FrameGeometry();
      const double x = toplevel->scene_tree->node.x + frame.x +
                       static_cast<double>(frame.width) / 2.0;
      const double y = toplevel->scene_tree->node.y + frame.y +
                       static_cast<double>(frame.height) / 2.0;
      wlr_cursor_warp_closest(cursor_, nullptr, x, y);
      BeginInteractive(toplevel, CursorMode::kMove, 0);
      ProcessCursorMotion(0);
      break;
    }
    case view::WindowMenu::Action::kResize: {
      const wlr_box frame = toplevel->FrameGeometry();
      const double x = toplevel->scene_tree->node.x + frame.x + frame.width + 8;
      const double y =
          toplevel->scene_tree->node.y + frame.y + frame.height + 8;
      wlr_cursor_warp_closest(cursor_, nullptr, x, y);
      BeginInteractive(toplevel, CursorMode::kResize,
                       WLR_EDGE_RIGHT | WLR_EDGE_BOTTOM);
      ProcessCursorMotion(0);
      break;
    }
    case view::WindowMenu::Action::kToggleKeepAbove:
      SetKeptAbove(toplevel, !toplevel->kept_above);
      break;
    case view::WindowMenu::Action::kToggleAllWorkspaces:
      SetAllWorkspaces(toplevel, !toplevel->all_workspaces);
      break;
    case view::WindowMenu::Action::kMoveWorkspaceLeft:
      SetAllWorkspaces(toplevel, false);
      MoveToplevelToWorkspace(toplevel, std::max(0, toplevel->workspace - 1));
      break;
    case view::WindowMenu::Action::kMoveWorkspaceRight:
      SetAllWorkspaces(toplevel, false);
      MoveToplevelToWorkspace(
          toplevel, std::min(workspace_count_ - 1, toplevel->workspace + 1));
      break;
    case view::WindowMenu::Action::kClose:
      toplevel->Close();
      break;
  }
}

void CompositorPrivate::SetMultitaskingSourcesHidden(bool hidden) {
  multitasking_sources_hidden_ = hidden;
  for (const std::unique_ptr<Toplevel>& toplevel : toplevels_) {
    if (toplevel->scene_tree != nullptr) {
      wlr_scene_node_set_enabled(&toplevel->scene_tree->node,
                                 IsToplevelVisible(toplevel.get()));
    }
  }
}

void CompositorPrivate::SetWindowPreviewsSourcesHidden(bool hidden) {
  window_previews_sources_hidden_ = hidden;
  for (const std::unique_ptr<Toplevel>& toplevel : toplevels_) {
    if (toplevel->scene_tree != nullptr) {
      wlr_scene_node_set_enabled(&toplevel->scene_tree->node,
                                 IsToplevelVisible(toplevel.get()));
    }
  }
}

void CompositorPrivate::FocusLayerSurface(LayerSurface* layer_surface) {
  if ((protocol_manager_ != nullptr && protocol_manager_->SessionLocked()) ||
      layer_surface == nullptr || !layer_surface->mapped ||
      layer_surface->handle == nullptr ||
      layer_surface->handle->current.keyboard_interactive ==
          ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE) {
    return;
  }

  for (const std::unique_ptr<Toplevel>& toplevel : toplevels_) {
    if (toplevel->ssd != nullptr) {
      toplevel->ssd->SetActive(false);
    }
    UpdateCsdShadow(toplevel.get());
  }

  wlr_surface* surface = layer_surface->handle->surface;
  wlr_surface* previous_surface = seat_->keyboard_state.focused_surface;
  if (previous_surface == surface) {
    return;
  }
  if (previous_surface != nullptr) {
    wlr_xdg_toplevel* previous =
        wlr_xdg_toplevel_try_from_wlr_surface(previous_surface);
    if (previous != nullptr && previous->base->initialized) {
      wlr_xdg_toplevel_set_activated(previous, false);
    }
  }
  ParkXWaylandFocus();

  if (wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat_);
      keyboard != nullptr) {
    wlr_seat_keyboard_notify_enter(seat_, surface, keyboard->keycodes,
                                   keyboard->num_keycodes,
                                   &keyboard->modifiers);
  }
  if (protocol_manager_ != nullptr) {
    protocol_manager_->UpdateKeyboardFocus(surface);
    protocol_manager_->UpdateToplevel(previous_surface);
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
  const wlr_box old_usable = output->usable_box;
  output->usable_box = usable_box;
  output->usable_box.x += output_box.x;
  output->usable_box.y += output_box.y;
  if (protocol_manager_ != nullptr) {
    protocol_manager_->UpdateOutputUsableAreas();
  }

  FitToplevelsToUsableArea(output, old_usable);
}

void CompositorPrivate::FitToplevelsToUsableArea(Output* output,
                                                 const wlr_box& old_usable) {
  const wlr_box& usable = output->usable_box;
  if (usable.width <= 0 || usable.height <= 0) {
    return;
  }
  const bool changed = !wlr_box_equal(&old_usable, &usable);

  for (const std::unique_ptr<Toplevel>& toplevel : toplevels_) {
    if (!toplevel->IsAlive() || toplevel->scene_tree == nullptr) {
      continue;
    }

    // Maximized and tiled windows follow changes made by panels and other
    // exclusive layers.
    if (toplevel->maximized) {
      if (toplevel->maximized_output == output->handle) {
        toplevel->maximized_box = usable;
        toplevel->Configure(usable);
      }
      continue;
    }
    if (toplevel->tiled) {
      if (toplevel->tiled_output == output->handle && changed) {
        toplevel->tiled_box = TileBox(usable, toplevel->tile);
        toplevel->tile_position_pending = true;
        toplevel->Configure(toplevel->tiled_box);
      }
      continue;
    }

    // Floating windows only move when a newly reserved strip now covers an
    // edge that used to be usable. Windows placed partly off the old usable
    // area on purpose stay where they are.
    if (!changed || old_usable.width <= 0 || old_usable.height <= 0 ||
        !toplevel->mapped || toplevel->minimized || !toplevel->CanManage() ||
        toplevel->RequestedFullscreen()) {
      continue;
    }
    const wlr_box box = toplevel->LayoutFrame();
    if (box.width <= 0 || box.height <= 0) {
      continue;
    }
    wlr_box overlap = {};
    if (!wlr_box_intersection(&overlap, &box, &old_usable)) {
      continue;
    }

    // The top-left edge wins so the titlebar stays reachable.
    const int usable_right = usable.x + usable.width;
    const int usable_bottom = usable.y + usable.height;
    int x = box.x;
    int y = box.y;
    if (box.x + box.width > usable_right &&
        box.x + box.width <= old_usable.x + old_usable.width) {
      x = std::max(usable_right - box.width, usable.x);
    }
    if (box.x < usable.x && box.x >= old_usable.x) {
      x = usable.x;
    }
    if (box.y + box.height > usable_bottom &&
        box.y + box.height <= old_usable.y + old_usable.height) {
      y = std::max(usable_bottom - box.height, usable.y);
    }
    if (box.y < usable.y && box.y >= old_usable.y) {
      y = usable.y;
    }
    if (x == box.x && y == box.y) {
      continue;
    }

    MoveToplevelFrame(toplevel.get(), x, y);
  }
}

void CompositorPrivate::MoveToplevelFrame(Toplevel* toplevel, int x, int y) {
  const wlr_box frame = toplevel->LayoutFrame();
  if (toplevel->IsXWayland()) {
    toplevel->Configure(
        {.x = x, .y = y, .width = frame.width, .height = frame.height});
  } else {
    wlr_scene_node_set_position(&toplevel->scene_tree->node,
                                toplevel->scene_tree->node.x + x - frame.x,
                                toplevel->scene_tree->node.y + y - frame.y);
  }
  if (protocol_manager_ != nullptr) {
    protocol_manager_->UpdateToplevel(toplevel->Surface());
  }
}

void CompositorPrivate::ConstrainToUsableArea(Toplevel* toplevel) {
  // Keep a floating window's frame out of space reserved by panels. Axes the
  // window does not fit in keep their top-left edge reachable.
  if (toplevel == nullptr || !toplevel->IsAlive() ||
      toplevel->scene_tree == nullptr || toplevel->maximized ||
      toplevel->tiled || !toplevel->CanManage() ||
      toplevel->RequestedFullscreen()) {
    return;
  }
  const wlr_box frame = toplevel->LayoutFrame();
  if (frame.width <= 0 || frame.height <= 0) {
    return;
  }
  const int x = frame.x;
  const int y = frame.y;
  wlr_output* output = wlr_output_layout_output_at(
      output_layout_, x + frame.width / 2.0, y + frame.height / 2.0);
  if (output == nullptr) {
    output = wlr_output_layout_output_at(output_layout_, x, y);
  }
  if (output == nullptr) {
    return;
  }
  const wlr_box usable = UsableOutputBox(output);
  if (usable.width <= 0 || usable.height <= 0) {
    return;
  }
  const int new_x =
      std::max(std::min(x, usable.x + usable.width - frame.width), usable.x);
  const int new_y =
      std::max(std::min(y, usable.y + usable.height - frame.height), usable.y);
  if (new_x != x || new_y != y) {
    MoveToplevelFrame(toplevel, new_x, new_y);
  }
}

void CompositorPrivate::SetMaximized(Toplevel* toplevel, bool maximized) {
  // State changes need both a live protocol handle and a scene node.
  if (toplevel == nullptr || !toplevel->IsAlive() ||
      toplevel->scene_tree == nullptr) {
    return;
  }
  if (tile_animation_ != nullptr) tile_animation_->Cancel();

  // An explicit unmaximize request also leaves a tiling slot.
  if (maximized == toplevel->maximized) {
    if (!maximized && toplevel->tiled) {
      toplevel->tiled = false;
      toplevel->tile_position_pending = false;
      if (toplevel->ssd != nullptr) toplevel->ssd->SetTiled(false);
      toplevel->SetTiledState(0);
      if (toplevel->has_restore_box) {
        toplevel->Configure(toplevel->restore_box);
        toplevel->restore_position_pending = true;
      }
    }
    if (toplevel->ssd != nullptr) {
      toplevel->ssd->SetMaximized(maximized);
    }
    toplevel->SetMaximizedState(maximized);
    return;
  }

  const wlr_box frame = toplevel->FrameGeometry();
  if (maximized && !toplevel->tiled) {
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

  toplevel->tiled = false;
  toplevel->tile_position_pending = false;
  if (toplevel->ssd != nullptr) toplevel->ssd->SetTiled(false);
  toplevel->SetTiledState(0);

  toplevel->maximized = maximized;
  if (toplevel->ssd != nullptr) {
    toplevel->ssd->SetMaximized(maximized);
  }
  RebuildSurfaceClip(toplevel);
  UpdateCsdShadow(toplevel);

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
  if (protocol_manager_ != nullptr) {
    protocol_manager_->UpdateToplevel(toplevel->Surface());
  }
}

void CompositorPrivate::ToggleMaximized(Toplevel* toplevel) {
  // Used by titlebar double click.
  if (toplevel != nullptr && toplevel->CanMaximize()) {
    SetMaximized(toplevel, !toplevel->maximized);
  }
}

void CompositorPrivate::TileToplevel(Toplevel* toplevel,
                                     view::SplitScreenSwitcher::Tile tile) {
  if (toplevel == nullptr || !toplevel->mapped || !toplevel->IsAlive() ||
      !toplevel->CanManage() || toplevel->scene_tree == nullptr) {
    return;
  }
  wlr_output* output =
      wlr_output_layout_output_at(output_layout_, cursor_->x, cursor_->y);
  if (output == nullptr) {
    output = wlr_output_layout_get_center_output(output_layout_);
  }
  if (output == nullptr) return;
  const wlr_box usable = UsableOutputBox(output);
  if (usable.width <= 1 || usable.height <= 1) return;

  if (!toplevel->maximized && !toplevel->tiled) {
    const wlr_box frame = toplevel->FrameGeometry();
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

  const wlr_box target = TileBox(usable, tile);

  if (tile_animation_ != nullptr) {
    wlr_box animated_from = toplevel->FrameGeometry();
    animated_from.x += toplevel->scene_tree->node.x;
    animated_from.y += toplevel->scene_tree->node.y;
    tile_animation_->Start(toplevel->scene_tree, animated_from, target);
  }

  toplevel->maximized = false;
  toplevel->maximized_output = nullptr;
  toplevel->tiled = true;
  toplevel->tiled_box = target;
  toplevel->tile = tile;
  toplevel->tiled_output = output;
  toplevel->tile_position_pending = true;
  toplevel->restore_position_pending = false;
  if (toplevel->ssd != nullptr) {
    toplevel->ssd->SetMaximized(false);
    toplevel->ssd->SetTiled(true);
  }
  RebuildSurfaceClip(toplevel);
  UpdateCsdShadow(toplevel);
  toplevel->SetTiledState(WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT |
                          WLR_EDGE_RIGHT);
  toplevel->SetMaximizedState(false);
  toplevel->Configure(target);
  if (toplevel->minimized) {
    toplevel->minimized = false;
    toplevel->SetMinimizedState(false);
    wlr_scene_node_set_enabled(&toplevel->scene_tree->node, true);
  }
  FocusToplevel(toplevel);
  if (protocol_manager_ != nullptr) {
    protocol_manager_->UpdateToplevel(toplevel->Surface());
  }
}

void CompositorPrivate::Minimize(Toplevel* toplevel) {
  // A minimized window stays mapped but is removed from the scene.
  if (toplevel == nullptr || !toplevel->CanMinimize() || !toplevel->mapped ||
      toplevel->minimized || toplevel->scene_tree == nullptr) {
    return;
  }
  if (tile_animation_ != nullptr) {
    tile_animation_->Cancel();
  }
  toplevel->minimized = true;
  toplevel->SetMinimizedState(true);
  wlr_scene_node_set_enabled(&toplevel->scene_tree->node, false);
  // Move keyboard focus away if this was the active window.
  if (seat_->keyboard_state.focused_surface == toplevel->Surface()) {
    FocusNextToplevel(toplevel);
  }
  // An X toplevel stays activated under shell layers
  if (toplevel->IsXWayland()) {
    toplevel->SetActivated(false);
  }
  wlr_seat_pointer_clear_focus(seat_);
  if (protocol_manager_ != nullptr) {
    protocol_manager_->UpdateToplevel(toplevel->Surface());
  }
}

void CompositorPrivate::RestoreForMove(Toplevel* toplevel) {
  // Dragging a maximized or tiled window begins at its previous normal size.
  if (toplevel == nullptr || (!toplevel->maximized && !toplevel->tiled) ||
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

view::Ssd::HitTarget CompositorPrivate::CsdResizeHitAt(
    Toplevel** hit_toplevel) const {
  if (hit_toplevel != nullptr) {
    *hit_toplevel = nullptr;
  }

  // For a GTK CSD decor, the round corner may be affecting its resize zone
  wlr_scene_node* node = nullptr;
  wl_list_for_each_reverse(node, &toplevel_tree_->children, link) {
    auto found = std::find_if(toplevels_.begin(), toplevels_.end(),
                              [node](const auto& candidate) {
                                return candidate->scene_tree != nullptr &&
                                       &candidate->scene_tree->node == node;
                              });
    if (found == toplevels_.end()) {
      continue;
    }
    Toplevel* candidate = found->get();
    if (candidate == nullptr || !candidate->IsAlive() ||
        candidate->IsXWayland() || candidate->ssd != nullptr ||
        candidate->scene_tree == nullptr || !candidate->mapped ||
        candidate->minimized || candidate->maximized || candidate->tiled ||
        candidate->RequestedFullscreen()) {
      continue;
    }

    const wlr_box geometry = candidate->Geometry();
    if (geometry.width <= 0 || geometry.height <= 0) {
      continue;
    }
    const int left = candidate->scene_tree->node.x;
    const int top = candidate->scene_tree->node.y;
    const int right = left + geometry.width;
    const int bottom = top + geometry.height;
    if (cursor_->x < left - kCsdResizeMargin ||
        cursor_->x >= right + kCsdResizeMargin ||
        cursor_->y < top - kCsdResizeMargin ||
        cursor_->y >= bottom + kCsdResizeMargin) {
      continue;
    }

    uint32_t edges = 0;
    if (cursor_->x < left + kCsdResizeMargin) {
      edges |= WLR_EDGE_LEFT;
    } else if (cursor_->x >= right - kCsdResizeMargin) {
      edges |= WLR_EDGE_RIGHT;
    }
    if (cursor_->y < top + kCsdResizeMargin) {
      edges |= WLR_EDGE_TOP;
    } else if (cursor_->y >= bottom - kCsdResizeMargin) {
      edges |= WLR_EDGE_BOTTOM;
    }

    const wlr_xdg_toplevel_state& state = candidate->handle->current;
    const bool fixed_width = state.min_width > 0 && state.max_width > 0 &&
                             state.min_width == state.max_width;
    const bool fixed_height = state.min_height > 0 && state.max_height > 0 &&
                              state.min_height == state.max_height;
    if (fixed_width) {
      edges &= ~(WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
    }
    if (fixed_height) {
      edges &= ~(WLR_EDGE_TOP | WLR_EDGE_BOTTOM);
    }
    if (edges == 0) {
      continue;
    }

    if (hit_toplevel != nullptr) {
      *hit_toplevel = candidate;
    }
    return {.part = view::Ssd::Part::kResize, .edges = edges};
  }
  return {};
}

void CompositorPrivate::ResetCursorMode() {
  // Drop all state left by a move or resize grab.
  if (cursor_mode_ == CursorMode::kResize && grabbed_toplevel_ != nullptr) {
    grabbed_toplevel_->SetResizingState(false);
  }
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
  if ((protocol_manager_ != nullptr && protocol_manager_->SessionLocked()) ||
      toplevel == nullptr || !toplevel->mapped ||
      toplevel->scene_tree == nullptr) {
    return;
  }
  if (tile_animation_ != nullptr) tile_animation_->Cancel();

  // Normalize a maximized or tiled window before it starts moving.
  if (mode == CursorMode::kMove && (toplevel->maximized || toplevel->tiled)) {
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

  if (edges == 0 || toplevel->maximized || toplevel->RequestedFullscreen()) {
    ResetCursorMode();
    return;
  }
  toplevel->SetResizingState(true);
  SetCursorName(ResizeCursorName(edges));

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
    int y = cursor_->y - grab_y_;
    // Panels keep the titlebar reachable: the frame top stays inside the
    // usable area of the output under the pointer.
    const wlr_box usable = UsableOutputBox(
        wlr_output_layout_output_at(output_layout_, cursor_->x, cursor_->y));
    if (usable.width > 0 && usable.height > 0) {
      const int frame_offset = grabbed_toplevel_->LayoutFrame().y -
                               grabbed_toplevel_->scene_tree->node.y;
      const int min_top = usable.y;
      const int max_top =
          std::max(min_top, usable.y + usable.height - kMoveTitlebarVisible);
      y = std::clamp(y + frame_offset, min_top, max_top) - frame_offset;
    }
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
    // Growing upward stops at a top panel.
    const wlr_box usable = UsableOutputBox(
        wlr_output_layout_output_at(output_layout_, cursor_->x, cursor_->y));
    // grab_box_ is in Configure() coordinates, which count the window
    // geometry offset that the xdg scene tree has already applied.
    const int min_top = usable.y + grabbed_toplevel_->Geometry().y;
    if (usable.width > 0 && usable.height > 0 && grab_box_.y >= min_top) {
      top = std::min(std::max(top, min_top), bottom - 1);
    }
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
  if (window_menu_ != nullptr &&
      window_menu_->HandleMotion(cursor_->x, cursor_->y)) {
    SetCursorName("default");
    wlr_seat_pointer_clear_focus(seat_);
    return;
  }
  if (split_screen_switcher_ != nullptr &&
      split_screen_switcher_->HandleMotion(cursor_->x, cursor_->y)) {
    if (titlebar_tooltip_ != nullptr) titlebar_tooltip_->Cancel();
    SetCursorName("default");
    wlr_seat_pointer_clear_focus(seat_);
    return;
  }
  if (window_previews_ != nullptr &&
      window_previews_->HandleMotion(cursor_->x, cursor_->y)) {
    SetCursorName("default");
    wlr_seat_pointer_clear_focus(seat_);
    return;
  }
  if (multitasking_ != nullptr &&
      multitasking_->HandleMotion(cursor_->x, cursor_->y)) {
    SetCursorName("default");
    wlr_seat_pointer_clear_focus(seat_);
    return;
  }
  if (window_selector_ != nullptr && window_selector_->HandleMotion()) {
    wlr_seat_pointer_clear_focus(seat_);
    return;
  }
  // Interactive grabs own pointer motion until the button is released.
  if (cursor_mode_ != CursorMode::kPassthrough) {
    if (titlebar_tooltip_ != nullptr) titlebar_tooltip_->Cancel();
    if (split_screen_switcher_ != nullptr) {
      split_screen_switcher_->Cancel();
    }
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
  Toplevel* csd_resize_toplevel = nullptr;
  const view::Ssd::HitTarget csd_resize_hit =
      CsdResizeHitAt(&csd_resize_toplevel);
  if (csd_resize_hit.part == view::Ssd::Part::kResize) {
    if (titlebar_tooltip_ != nullptr) titlebar_tooltip_->Cancel();
    if (split_screen_switcher_ != nullptr) {
      split_screen_switcher_->LeaveMaximize();
    }
    SetCursorName(ResizeCursorName(csd_resize_hit.edges));
    wlr_seat_pointer_clear_focus(seat_);
    return;
  }
  if (ssd_hit.part == view::Ssd::Part::kMaximize && toplevel != nullptr &&
      toplevel->Surface() != nullptr) {
    if (titlebar_tooltip_ != nullptr) titlebar_tooltip_->Cancel();
    if (split_screen_switcher_ != nullptr) {
      const wlr_box geometry = toplevel->Geometry();
      split_screen_switcher_->HoverMaximize(
          toplevel->Surface(), cursor_->x, cursor_->y,
          toplevel->scene_tree->node.y + geometry.y);
    }
  } else {
    if (split_screen_switcher_ != nullptr) {
      split_screen_switcher_->LeaveMaximize();
    }
    view::TitlebarTooltip::Hint hint = view::TitlebarTooltip::Hint::kNone;
    if (ssd_hit.part == view::Ssd::Part::kMinimize) {
      hint = view::TitlebarTooltip::Hint::kMinimize;
    } else if (ssd_hit.part == view::Ssd::Part::kClose) {
      hint = view::TitlebarTooltip::Hint::kClose;
    }
    if (titlebar_tooltip_ != nullptr) {
      titlebar_tooltip_->Hover(
          toplevel == nullptr ? nullptr : toplevel->Surface(), hint, cursor_->x,
          cursor_->y);
    }
  }
  if (ssd_hit.part != view::Ssd::Part::kNone) {
    SetCursorName(ssd_hit.part == view::Ssd::Part::kResize
                      ? ResizeCursorName(ssd_hit.edges)
                      : "default");
    wlr_seat_pointer_clear_focus(seat_);
    return;
  }
  if (toplevel == nullptr) {
    SetCursorName("default");
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
  if (compositor->cursor_hidden_by_touch_) {
    compositor->SetCursorName("default");
    compositor->cursor_hidden_by_touch_ = false;
  }
  double delta_x = event->delta_x;
  double delta_y = event->delta_y;
  if (compositor->protocol_manager_ != nullptr) {
    compositor->protocol_manager_->NotifyPointer(event->time_msec);
    compositor->protocol_manager_->SendRelativeMotion(*event);
    compositor->protocol_manager_->ConfinePointer(&delta_x, &delta_y);
  }
  wlr_cursor_move(compositor->cursor_, &event->pointer->base, delta_x, delta_y);
  if (compositor->protocol_manager_ != nullptr) {
    compositor->protocol_manager_->MoveDrag();
  }
  compositor->ProcessCursorMotion(event->time_msec);
  if (compositor->shake_cursor_ != nullptr) {
    compositor->shake_cursor_->HandleMotion();
  }
}

void CompositorPrivate::OnCursorMotionAbsolute(
    CompositorPrivate* compositor, wlr_pointer_motion_absolute_event* event) {
  if (compositor->cursor_hidden_by_touch_) {
    compositor->SetCursorName("default");
    compositor->cursor_hidden_by_touch_ = false;
  }
  double layout_x = 0;
  double layout_y = 0;
  wlr_cursor_absolute_to_layout_coords(compositor->cursor_,
                                       &event->pointer->base, event->x,
                                       event->y, &layout_x, &layout_y);
  double delta_x = layout_x - compositor->cursor_->x;
  double delta_y = layout_y - compositor->cursor_->y;
  if (compositor->protocol_manager_ != nullptr) {
    compositor->protocol_manager_->NotifyPointer(event->time_msec);
    compositor->protocol_manager_->ConfinePointer(&delta_x, &delta_y);
  }
  wlr_cursor_warp_closest(compositor->cursor_, &event->pointer->base,
                          compositor->cursor_->x + delta_x,
                          compositor->cursor_->y + delta_y);
  if (compositor->protocol_manager_ != nullptr) {
    compositor->protocol_manager_->MoveDrag();
  }
  compositor->ProcessCursorMotion(event->time_msec);
  if (compositor->shake_cursor_ != nullptr) {
    compositor->shake_cursor_->HandleMotion();
  }
}

void CompositorPrivate::OnCursorButton(CompositorPrivate* compositor,
                                       wlr_pointer_button_event* event) {
  if (event->pointer != nullptr && compositor->cursor_hidden_by_touch_) {
    compositor->SetCursorName("default");
    compositor->cursor_hidden_by_touch_ = false;
  }
  if (compositor->protocol_manager_ != nullptr) {
    compositor->protocol_manager_->NotifyPointer(event->time_msec);
  }
  if (compositor->protocol_manager_ != nullptr &&
      compositor->protocol_manager_->SessionLocked()) {
    wlr_seat_pointer_notify_button(compositor->seat_, event->time_msec,
                                   event->button, event->state);
    return;
  }
  if (compositor->window_menu_ != nullptr &&
      compositor->window_menu_->HandleButton(event->button, event->state)) {
    if (!compositor->window_menu_->IsActive()) {
      if (event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
        compositor->suppress_button_release_ = true;
      }
      compositor->ProcessCursorMotion(event->time_msec);
    }
    return;
  }
  if (compositor->split_screen_switcher_ != nullptr &&
      compositor->split_screen_switcher_->HandleButton(event->button,
                                                       event->state)) {
    return;
  }
  if (compositor->multitasking_ != nullptr &&
      compositor->multitasking_->HandleButton(event->button, event->state)) {
    if (!compositor->multitasking_->IsActive()) {
      compositor->ProcessCursorMotion(event->time_msec);
    }
    return;
  }
  if (compositor->window_previews_ != nullptr &&
      compositor->window_previews_->HandleButton(event->button, event->state)) {
    if (!compositor->window_previews_->IsActive()) {
      compositor->ProcessCursorMotion(event->time_msec);
    }
    return;
  }
  if (compositor->window_selector_ != nullptr &&
      compositor->window_selector_->HandleButton(event->button, event->state)) {
    if (!compositor->window_selector_->IsActive()) {
      compositor->ProcessCursorMotion(event->time_msec);
    }
    return;
  }
  // WLR mouse release event.
  if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
    for (const std::unique_ptr<Toplevel>& toplevel : compositor->toplevels_) {
      if (toplevel->ssd != nullptr) {
        toplevel->ssd->SetPressed({});
      }
    }
    if (event->button == BTN_RIGHT &&
        compositor->pending_window_menu_ != nullptr) {
      Toplevel* target = compositor->pending_window_menu_;
      compositor->pending_window_menu_ = nullptr;
      compositor->suppress_button_release_ = false;
      compositor->EndInteractive();
      compositor->ShowWindowMenu(target, compositor->cursor_->x,
                                 compositor->cursor_->y);
      return;
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

  // Menu-driven move/resize operates without a held pointer button. The next
  // press confirms the position and its matching release stays compositor
  // side instead of leaking to the client below it.
  if (compositor->cursor_mode_ != CursorMode::kPassthrough) {
    compositor->EndInteractive();
    compositor->suppress_button_release_ = true;
    return;
  }

  double surface_x = 0;
  double surface_y = 0;
  wlr_surface* surface = nullptr;
  Toplevel* toplevel =
      compositor->ToplevelAt(compositor->cursor_->x, compositor->cursor_->y,
                             &surface, &surface_x, &surface_y);
  const view::Ssd::HitTarget ssd_hit = compositor->SsdHitAt(toplevel);
  Toplevel* csd_resize_toplevel = nullptr;
  const view::Ssd::HitTarget csd_resize_hit =
      compositor->CsdResizeHitAt(&csd_resize_toplevel);

  if (event->button == BTN_LEFT &&
      csd_resize_hit.part == view::Ssd::Part::kResize &&
      csd_resize_toplevel != nullptr) {
    compositor->FocusToplevel(csd_resize_toplevel);
    compositor->suppress_button_release_ = true;
    compositor->last_click_toplevel_ = nullptr;
    compositor->BeginInteractive(csd_resize_toplevel, CursorMode::kResize,
                                 csd_resize_hit.edges);
    return;
  }

  if (ssd_hit.part != view::Ssd::Part::kNone) {
    compositor->FocusToplevel(toplevel);
    compositor->suppress_button_release_ = true;
    if (event->button != BTN_LEFT) {
      compositor->pending_window_menu_ =
          event->button == BTN_RIGHT &&
                  ssd_hit.part == view::Ssd::Part::kTitlebar
              ? toplevel
              : nullptr;
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
      if (compositor->split_screen_switcher_ != nullptr) {
        compositor->split_screen_switcher_->Cancel();
      }
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
  if (event->pointer != nullptr && compositor->cursor_hidden_by_touch_) {
    compositor->SetCursorName("default");
    compositor->cursor_hidden_by_touch_ = false;
  }
  if (compositor->protocol_manager_ != nullptr) {
    compositor->protocol_manager_->NotifyPointer(event->time_msec);
    if (!compositor->protocol_manager_->ShouldForwardAxis(*event)) {
      return;
    }
  }
  if (compositor->window_menu_ != nullptr &&
      compositor->window_menu_->IsActive()) {
    return;
  }
  if (compositor->multitasking_ != nullptr &&
      compositor->multitasking_->IsActive()) {
    return;
  }
  if (compositor->window_previews_ != nullptr &&
      compositor->window_previews_->IsActive()) {
    return;
  }
  // Forward wheel and touchpad axis events without changing their source.
  wlr_seat_pointer_notify_axis(
      compositor->seat_, event->time_msec, event->orientation, event->delta,
      event->delta_discrete, event->source, event->relative_direction);
}

void CompositorPrivate::OnCursorFrame(CompositorPrivate* compositor, void*) {
  // Group all pointer events received in the current backend frame.
  wlr_seat_pointer_notify_frame(compositor->seat_);
}

void CompositorPrivate::OnTouchDown(CompositorPrivate* compositor,
                                    wlr_touch_down_event* event) {
  if (compositor->protocol_manager_ != nullptr)
    compositor->protocol_manager_->TouchGestureDown(
        event->touch, event->touch_id, event->x, event->y);
  compositor->MoveTouchCursor(event->touch, event->x, event->y);
  if (compositor->multitasking_ != nullptr &&
      compositor->multitasking_->IsActive()) {
    compositor->multitasking_->HandleMotion(compositor->cursor_->x,
                                            compositor->cursor_->y);
    compositor->multitasking_->HandleButton(BTN_LEFT,
                                            WL_POINTER_BUTTON_STATE_PRESSED);
    compositor->touch_points_.push_back({
        .touch = event->touch,
        .touch_id = event->touch_id,
        .mode = TouchPointMode::kMultitasking,
        .last_x = compositor->cursor_->x,
        .last_y = compositor->cursor_->y,
    });
    return;
  }
  if (compositor->window_previews_ != nullptr &&
      compositor->window_previews_->IsActive()) {
    compositor->window_previews_->HandleMotion(compositor->cursor_->x,
                                               compositor->cursor_->y);
    compositor->window_previews_->HandleButton(BTN_LEFT,
                                               WL_POINTER_BUTTON_STATE_PRESSED);
    compositor->touch_points_.push_back({
        .touch = event->touch,
        .touch_id = event->touch_id,
        .mode = TouchPointMode::kWindowPreviews,
        .last_x = compositor->cursor_->x,
        .last_y = compositor->cursor_->y,
    });
    return;
  }
  if (compositor->window_selector_ != nullptr &&
      compositor->window_selector_->IsActive()) {
    compositor->window_selector_->HandleMotion();
    compositor->window_selector_->HandleButton(BTN_LEFT,
                                               WL_POINTER_BUTTON_STATE_PRESSED);
    compositor->touch_points_.push_back({
        .touch = event->touch,
        .touch_id = event->touch_id,
        .mode = TouchPointMode::kSelector,
        .last_x = compositor->cursor_->x,
        .last_y = compositor->cursor_->y,
    });
    return;
  }
  compositor->cursor_hidden_by_touch_ = true;
  compositor->UnsetCursorImage();

  double surface_x = 0;
  double surface_y = 0;
  wlr_surface* surface = nullptr;
  Toplevel* toplevel =
      compositor->ToplevelAt(compositor->cursor_->x, compositor->cursor_->y,
                             &surface, &surface_x, &surface_y);

  TouchPointMode mode = TouchPointMode::kIgnored;
  if (surface != nullptr &&
      wlr_surface_accepts_touch(surface, compositor->seat_)) {
    if (compositor->protocol_manager_ != nullptr) {
      compositor->protocol_manager_->NotifyTouch(surface, event->time_msec);
    }
    if (wlr_seat_touch_notify_down(compositor->seat_, surface, event->time_msec,
                                   event->touch_id, surface_x,
                                   surface_y) != 0) {
      mode = TouchPointMode::kNative;
      if (toplevel != nullptr) {
        compositor->FocusToplevel(toplevel);
      } else {
        compositor->FocusLayerSurface(compositor->LayerSurfaceFor(surface));
      }
    }
  }

  const bool pointer_in_use =
      std::any_of(compositor->touch_points_.begin(),
                  compositor->touch_points_.end(), [](const TouchPoint& point) {
                    return point.mode == TouchPointMode::kPointer;
                  });
  if (mode == TouchPointMode::kIgnored && !pointer_in_use) {
    compositor->ProcessCursorMotion(event->time_msec);
    compositor->UnsetCursorImage();
    compositor->SendPointerTouchButton(event->time_msec,
                                       WL_POINTER_BUTTON_STATE_PRESSED);
    mode = TouchPointMode::kPointer;
  }

  compositor->touch_points_.push_back({
      .touch = event->touch,
      .touch_id = event->touch_id,
      .mode = mode,
      .last_x = compositor->cursor_->x,
      .last_y = compositor->cursor_->y,
  });
  if (compositor->touch_feedback_ != nullptr) {
    compositor->touch_feedback_->Down(
        event->touch, event->touch_id,
        {.x = compositor->cursor_->x, .y = compositor->cursor_->y});
  }
}

void CompositorPrivate::OnTouchUp(CompositorPrivate* compositor,
                                  wlr_touch_up_event* event) {
  if (compositor->protocol_manager_ != nullptr)
    compositor->protocol_manager_->TouchGestureUp(event->touch, event->touch_id,
                                                  false);
  TouchPoint* point = compositor->FindTouchPoint(event->touch, event->touch_id);
  if (point == nullptr) {
    return;
  }
  if (point->mode == TouchPointMode::kNative) {
    wlr_touch_point* seat_point =
        wlr_seat_touch_get_point(compositor->seat_, event->touch_id);
    if (compositor->protocol_manager_ != nullptr) {
      compositor->protocol_manager_->NotifyTouch(
          seat_point == nullptr ? nullptr : seat_point->surface,
          event->time_msec);
    }
    wlr_seat_touch_notify_up(compositor->seat_, event->time_msec,
                             event->touch_id);
  } else if (point->mode == TouchPointMode::kPointer) {
    compositor->SendPointerTouchButton(event->time_msec,
                                       WL_POINTER_BUTTON_STATE_RELEASED);
  } else if (point->mode == TouchPointMode::kSelector &&
             compositor->window_selector_ != nullptr) {
    compositor->window_selector_->HandleButton(
        BTN_LEFT, WL_POINTER_BUTTON_STATE_RELEASED);
  } else if (point->mode == TouchPointMode::kMultitasking &&
             compositor->multitasking_ != nullptr) {
    compositor->multitasking_->HandleButton(BTN_LEFT,
                                            WL_POINTER_BUTTON_STATE_RELEASED);
  } else if (point->mode == TouchPointMode::kWindowPreviews &&
             compositor->window_previews_ != nullptr) {
    compositor->window_previews_->HandleButton(
        BTN_LEFT, WL_POINTER_BUTTON_STATE_RELEASED);
  }
  std::erase_if(compositor->touch_points_, [&](const TouchPoint& candidate) {
    return candidate.touch == event->touch &&
           candidate.touch_id == event->touch_id;
  });
  if (compositor->touch_feedback_ != nullptr) {
    compositor->touch_feedback_->Up(event->touch, event->touch_id);
  }
}

void CompositorPrivate::OnTouchMotion(CompositorPrivate* compositor,
                                      wlr_touch_motion_event* event) {
  if (compositor->protocol_manager_ != nullptr)
    compositor->protocol_manager_->TouchGestureMotion(
        event->touch, event->touch_id, event->x, event->y);
  TouchPoint* point = compositor->FindTouchPoint(event->touch, event->touch_id);
  if (point == nullptr) {
    return;
  }
  compositor->MoveTouchCursor(event->touch, event->x, event->y);
  const double delta_x = compositor->cursor_->x - point->last_x;
  const double delta_y = compositor->cursor_->y - point->last_y;
  point->last_x = compositor->cursor_->x;
  point->last_y = compositor->cursor_->y;

  if (point->mode == TouchPointMode::kNative) {
    wlr_touch_point* seat_point =
        wlr_seat_touch_get_point(compositor->seat_, event->touch_id);
    if (seat_point != nullptr) {
      if (compositor->protocol_manager_ != nullptr) {
        compositor->protocol_manager_->NotifyTouch(seat_point->surface,
                                                   event->time_msec);
      }
      wlr_seat_touch_notify_motion(compositor->seat_, event->time_msec,
                                   event->touch_id, seat_point->sx + delta_x,
                                   seat_point->sy + delta_y);
    }
  } else if (point->mode == TouchPointMode::kPointer) {
    if (compositor->protocol_manager_ != nullptr) {
      compositor->protocol_manager_->NotifyPointer(event->time_msec);
    }
    compositor->ProcessCursorMotion(event->time_msec);
    compositor->UnsetCursorImage();
    compositor->touch_pointer_frame_pending_ = true;
  } else if (point->mode == TouchPointMode::kSelector &&
             compositor->window_selector_ != nullptr) {
    compositor->window_selector_->HandleMotion();
  } else if (point->mode == TouchPointMode::kMultitasking &&
             compositor->multitasking_ != nullptr) {
    compositor->multitasking_->HandleMotion(compositor->cursor_->x,
                                            compositor->cursor_->y);
  } else if (point->mode == TouchPointMode::kWindowPreviews &&
             compositor->window_previews_ != nullptr) {
    compositor->window_previews_->HandleMotion(compositor->cursor_->x,
                                               compositor->cursor_->y);
  }
  if (compositor->touch_feedback_ != nullptr) {
    compositor->touch_feedback_->Motion(
        event->touch, event->touch_id,
        {.x = compositor->cursor_->x, .y = compositor->cursor_->y});
  }
}

void CompositorPrivate::OnTouchCancel(CompositorPrivate* compositor,
                                      wlr_touch_cancel_event* event) {
  if (compositor->protocol_manager_ != nullptr)
    compositor->protocol_manager_->TouchGestureUp(event->touch, event->touch_id,
                                                  true);
  TouchPoint* point = compositor->FindTouchPoint(event->touch, event->touch_id);
  if (point == nullptr) {
    return;
  }
  if (point->mode == TouchPointMode::kNative) {
    wlr_touch_point* seat_point =
        wlr_seat_touch_get_point(compositor->seat_, event->touch_id);
    if (seat_point != nullptr && seat_point->client != nullptr) {
      wlr_seat_touch_notify_cancel(compositor->seat_, seat_point->client);
    }
  } else if (point->mode == TouchPointMode::kPointer) {
    compositor->SendPointerTouchButton(event->time_msec,
                                       WL_POINTER_BUTTON_STATE_RELEASED);
  } else if (point->mode == TouchPointMode::kSelector &&
             compositor->window_selector_ != nullptr) {
    compositor->window_selector_->Cancel();
  } else if (point->mode == TouchPointMode::kMultitasking &&
             compositor->multitasking_ != nullptr) {
    compositor->multitasking_->Cancel();
  } else if (point->mode == TouchPointMode::kWindowPreviews &&
             compositor->window_previews_ != nullptr) {
    compositor->window_previews_->Cancel();
  }
  std::erase_if(compositor->touch_points_, [&](const TouchPoint& candidate) {
    const bool remove =
        candidate.touch == event->touch &&
        (candidate.touch_id == event->touch_id ||
         (candidate.mode == TouchPointMode::kNative &&
          wlr_seat_touch_get_point(compositor->seat_, candidate.touch_id) ==
              nullptr));
    if (remove && compositor->touch_feedback_ != nullptr) {
      compositor->touch_feedback_->Cancel(candidate.touch, candidate.touch_id);
    }
    return remove;
  });
}

void CompositorPrivate::OnTouchFrame(CompositorPrivate* compositor, void*) {
  wlr_seat_touch_notify_frame(compositor->seat_);
  if (compositor->touch_pointer_frame_pending_) {
    wlr_seat_pointer_notify_frame(compositor->seat_);
    compositor->touch_pointer_frame_pending_ = false;
  }
}

void CompositorPrivate::OnRequestCursor(
    CompositorPrivate* compositor,
    wlr_seat_pointer_request_set_cursor_event* event) {
  // Only the focused client is allowed to replace the cursor surface.
  if (compositor->seat_->pointer_state.focused_client == event->seat_client) {
    compositor->SetCursorSurface(event->surface, event->hotspot_x,
                                 event->hotspot_y);
  }
}

void CompositorPrivate::SetCursorName(const char* name) {
  cursor_surface_destroy_.Disconnect();
  cursor_image_ = {.kind = CursorImage::Kind::kName, .name = name};
  if (!cursor_image_locked_) ApplyCursorImage();
}

void CompositorPrivate::SetCursorSurface(wlr_surface* surface,
                                         int32_t hotspot_x, int32_t hotspot_y) {
  cursor_surface_destroy_.Disconnect();
  if (surface == nullptr) {
    cursor_image_ = {.kind = CursorImage::Kind::kNone};
  } else {
    cursor_image_ = {.kind = CursorImage::Kind::kSurface,
                     .surface = surface,
                     .hotspot_x = hotspot_x,
                     .hotspot_y = hotspot_y};
    cursor_surface_destroy_.Connect(&surface->events.destroy);
  }
  if (!cursor_image_locked_) ApplyCursorImage();
}

void CompositorPrivate::UnsetCursorImage() {
  cursor_surface_destroy_.Disconnect();
  cursor_image_ = {.kind = CursorImage::Kind::kNone};
  if (!cursor_image_locked_) ApplyCursorImage();
}

void CompositorPrivate::LockCursorImage(bool locked) {
  if (cursor_image_locked_ == locked) return;
  cursor_image_locked_ = locked;
  if (locked) {
    wlr_cursor_unset_image(cursor_);
  } else {
    ApplyCursorImage();
  }
}

void CompositorPrivate::ApplyCursorImage() {
  switch (cursor_image_.kind) {
    case CursorImage::Kind::kName:
      wlr_cursor_set_xcursor(cursor_, cursor_manager_,
                             cursor_image_.name.c_str());
      break;
    case CursorImage::Kind::kSurface:
      wlr_cursor_set_surface(cursor_, cursor_image_.surface,
                             cursor_image_.hotspot_x, cursor_image_.hotspot_y);
      break;
    case CursorImage::Kind::kNone:
      wlr_cursor_unset_image(cursor_);
      break;
  }
}

void CompositorPrivate::OnCursorSurfaceDestroy(CompositorPrivate* compositor,
                                               void*) {
  // wlr_cursor drops a destroyed cursor surface on its own; only forget it.
  compositor->cursor_surface_destroy_.Disconnect();
  compositor->cursor_image_ = {.kind = CursorImage::Kind::kNone};
}

void CompositorPrivate::OnPointerFocusChange(
    CompositorPrivate* compositor, wlr_seat_pointer_focus_change_event* event) {
  // Restore the default cursor after leaving all client surfaces.
  if (event->new_surface == nullptr) {
    compositor->SetCursorName("default");
  }
  if (compositor->protocol_manager_ != nullptr) {
    compositor->protocol_manager_->UpdatePointerFocus(event->new_surface);
  }
}

void CompositorPrivate::OnRequestSelection(
    CompositorPrivate* compositor,
    wlr_seat_request_set_selection_event* event) {
  // The seat validates and owns the regular clipboard source.
  wlr_seat_set_selection(compositor->seat_, event->source, event->serial);
  if (compositor->dbus_manager_ != nullptr) {
    compositor->dbus_manager_->NotifySelectionChanged(
        false, compositor->seat_->keyboard_state.focused_surface);
  }
}

void CompositorPrivate::OnRequestPrimarySelection(
    CompositorPrivate* compositor,
    wlr_seat_request_set_primary_selection_event* event) {
  // Primary selection is handled separately from the regular clipboard.
  wlr_seat_set_primary_selection(compositor->seat_, event->source,
                                 event->serial);
  if (compositor->dbus_manager_ != nullptr) {
    compositor->dbus_manager_->NotifySelectionChanged(
        true, compositor->seat_->keyboard_state.focused_surface);
  }
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
  if (wlr_output_mode* mode = HighestNativeRefreshMode(output);
      mode != nullptr) {
    wlr_output_state_set_mode(&output_state, mode);
    ABSL_LOG(INFO) << "Selecting " << mode->width << 'x' << mode->height
                   << " at " << mode->refresh / 1000.0 << " Hz for "
                   << output->name;
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
  compositor->UpdateQtFrameInterval();
  compositor->ArrangeLayers(output_wrapper);
  if (compositor->protocol_manager_ != nullptr) {
    compositor->protocol_manager_->AddOutput(output);
  }
  if (compositor->dbus_manager_ != nullptr) {
    compositor->dbus_manager_->AddOutput(output);
  }
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
  compositor->RebuildSurfaceClip(toplevel.get());

  // Connect lifecycle and window management requests.
  toplevel->map.Connect(&handle->base->surface->events.map);
  toplevel->unmap.Connect(&handle->base->surface->events.unmap);
  toplevel->commit.Connect(&handle->base->surface->events.commit);
  toplevel->destroy.Connect(&handle->events.destroy);
  toplevel->request_move.Connect(&handle->events.request_move);
  toplevel->request_resize.Connect(&handle->events.request_resize);
  toplevel->request_show_window_menu.Connect(
      &handle->events.request_show_window_menu);
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
  // GXWM applies a compositor-owned radius to Qt/DTK xdg_popup surfaces even
  // when the client only requests blur. Keep this independent from the parent
  // toplevel so an in-process menu receives the same mask as its blur.
  compositor->SetSurfaceRoundCorner(handle->base->surface, 8);
  // DTK5/6 menus delegate their border and shadow to the compositor through a
  // personalization window context; DTK2 menus draw their own client-side and
  // must be left alone (gxde-wlcom's client_has_window_context gate).
  if (compositor->protocol_manager_ != nullptr &&
      compositor->protocol_manager_->ClientHasWindowContext(
          handle->base->surface)) {
    popup->shadow = std::make_unique<view::PopupShadow>(popup->scene_tree);
    popup->shadow->SetDark(compositor->protocol_manager_->IsDarkTheme());
  }
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
  if (compositor->touch_feedback_ != nullptr) {
    compositor->touch_feedback_->Render();
  }
  if (compositor->titlebar_tooltip_ != nullptr) {
    compositor->titlebar_tooltip_->Render();
  }
  if (compositor->split_screen_switcher_ != nullptr) {
    compositor->split_screen_switcher_->Render();
  }
  return compositor->qt_frame_timer_ == nullptr
             ? 0
             : wl_event_source_timer_update(compositor->qt_frame_timer_,
                                            compositor->qt_frame_interval_ms_);
}

void CompositorPrivate::UpdateQtFrameInterval() {
  int32_t highest_refresh = 0;
  for (const std::unique_ptr<Output>& output : outputs_) {
    if (output->handle != nullptr && output->handle->enabled) {
      highest_refresh = std::max(highest_refresh, output->handle->refresh);
    }
  }

  qt_frame_interval_ms_ =
      highest_refresh > 0 ? std::max(1, 1000000 / highest_refresh) : 16;
  if (qt_frame_timer_ != nullptr) {
    wl_event_source_timer_update(qt_frame_timer_, qt_frame_interval_ms_);
  }
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

bool CompositorPrivate::RegisterDefaultKeyBindings() {
  key_binding_manager_ = std::make_unique<input::KeyBindingManager>();
  const auto register_binding =
      [this](const char* shortcut, input::KeyBindingType type,
             input::KeyBindingManager::Action action, const char* description) {
        return key_binding_manager_
            ->Register(shortcut, type, std::move(action), description)
            .has_value();
      };

  return register_binding(
             "Alt+Tab:no", input::KeyBindingType::kWindowSwitch,
             [this]() {
               if (window_menu_ != nullptr) window_menu_->Cancel();
               if (window_previews_ != nullptr) window_previews_->Cancel();
               if (multitasking_ != nullptr) multitasking_->Cancel();
               if (app_switcher_ != nullptr) app_switcher_->Cycle(false);
             },
             "Switch to next window") &&
         register_binding(
             "Alt+Shift+Tab:no", input::KeyBindingType::kWindowSwitch,
             [this]() {
               if (window_menu_ != nullptr) window_menu_->Cancel();
               if (window_previews_ != nullptr) window_previews_->Cancel();
               if (multitasking_ != nullptr) multitasking_->Cancel();
               if (app_switcher_ != nullptr) app_switcher_->Cycle(true);
             },
             "Switch to previous window") &&
         register_binding(
             "Super+S:no", input::KeyBindingType::kWindowSwitch,
             [this]() {
               if (window_menu_ != nullptr) window_menu_->Cancel();
               if (window_selector_ != nullptr) window_selector_->Cancel();
               if (app_switcher_ != nullptr) app_switcher_->Cancel();
               if (window_previews_ != nullptr) window_previews_->Cancel();
               if (multitasking_ != nullptr) multitasking_->Toggle();
             },
             "Show multitasking and workspace overview") &&
         register_binding(
             "Super+Tab:no", input::KeyBindingType::kWindowSwitch,
             [this]() {
               if (window_menu_ != nullptr) window_menu_->Cancel();
               if (window_selector_ != nullptr) window_selector_->Cancel();
               if (app_switcher_ != nullptr) app_switcher_->Cancel();
               if (window_previews_ != nullptr) window_previews_->Cancel();
               if (multitasking_ != nullptr) multitasking_->Toggle();
             },
             "Show multitasking and workspace overview") &&
         register_binding(
             "Super+A:no", input::KeyBindingType::kWindowSwitch,
             [this]() {
               if (window_menu_ != nullptr) window_menu_->Cancel();
               if (window_selector_ != nullptr) window_selector_->Cancel();
               if (app_switcher_ != nullptr) app_switcher_->Cancel();
               if (multitasking_ != nullptr) multitasking_->Cancel();
               if (window_previews_ != nullptr) window_previews_->Toggle();
             },
             "Show windows from all workspaces") &&
         register_binding(
             "Alt+F3:no", input::KeyBindingType::kWindowMenu,
             [this]() {
               Toplevel* toplevel =
                   ToplevelForSurface(seat_->keyboard_state.focused_surface);
               if (toplevel == nullptr || toplevel->scene_tree == nullptr) {
                 return;
               }
               const wlr_box frame = toplevel->FrameGeometry();
               ShowWindowMenu(toplevel,
                              toplevel->scene_tree->node.x + frame.x - 8,
                              toplevel->scene_tree->node.y + frame.y);
             },
             "Show the active window menu") &&
         register_binding(
             "Alt+F4:no", input::KeyBindingType::kWindowClose,
             [this]() {
               if (Toplevel* toplevel = ToplevelForSurface(
                       seat_->keyboard_state.focused_surface);
                   toplevel != nullptr && toplevel->IsAlive()) {
                 toplevel->Close();
               }
             },
             "Close the active window") &&
         register_binding(
             "Alt+F9:no", input::KeyBindingType::kWindowMinimize,
             [this]() {
               Minimize(
                   ToplevelForSurface(seat_->keyboard_state.focused_surface));
             },
             "Minimize the active window") &&
         register_binding(
             "Alt+F10:no", input::KeyBindingType::kWindowMaximize,
             [this]() {
               ToggleMaximized(
                   ToplevelForSurface(seat_->keyboard_state.focused_surface));
             },
             "Toggle the active window maximized state");
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
  new_virtual_keyboard_.Disconnect();
  new_input_.Disconnect();
  request_primary_selection_.Disconnect();
  request_selection_.Disconnect();
  pointer_focus_change_.Disconnect();
  request_cursor_.Disconnect();
  touch_frame_.Disconnect();
  touch_cancel_.Disconnect();
  touch_motion_.Disconnect();
  touch_up_.Disconnect();
  touch_down_.Disconnect();
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
  tile_animation_.reset();
  split_screen_switcher_.reset();
  titlebar_tooltip_.reset();
  window_menu_.reset();
  window_previews_.reset();
  multitasking_.reset();
  app_switcher_.reset();
  xdg_decorations_.clear();
  popups_.clear();
  layer_surfaces_.clear();
  toplevels_.clear();
  touch_points_.clear();
  touch_devices_.clear();
  keyboards_.clear();
  dbus_manager_.reset();
  key_binding_manager_.reset();
  selection_persist_.reset();
  outputs_.clear();

  // The XWM owns a Wayland client.
  // This also needs to be stopped.
  xwayland_.reset();
  unsetenv("DISPLAY");
  if (display_ != nullptr) {
    wl_display_destroy_clients(display_);
  }
  protocol_manager_.reset();
  window_selector_.reset();
  input_method_relay_.reset();
  touch_feedback_.reset();
  shake_cursor_.reset();

  // Clear scene nodes.
  if (scene_ != nullptr) {
    wlr_scene_node_destroy(&scene_->tree.node);
    scene_ = nullptr;
    scene_layout_ = nullptr;
    shell_layer_trees_.fill(nullptr);
    toplevel_tree_ = nullptr;
    session_lock_tree_ = nullptr;
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
    if (backdrop_blur_renderer_ != nullptr) {
      backdrop_blur_renderer_->SetAllocator(nullptr);
    }
    wlr_allocator_destroy(allocator_);
    allocator_ = nullptr;
  }

  // Clear renders.
  if (renderer_ != nullptr) {
    wlr_renderer_destroy(renderer_);
    renderer_ = nullptr;
  }
  backdrop_blur_renderer_.reset();

  // Clear Wlroots display backends.
  backend_owner_.reset();
  backend_ = nullptr;

  // Clear Wayland display.
  if (display_ != nullptr) {
    wl_display_destroy(display_);
    display_ = nullptr;
    compositor_ = nullptr;
    security_context_manager_ = nullptr;
    explicit_sync_manager_ = nullptr;
    gamma_control_manager_ = nullptr;
    color_manager_ = nullptr;
    layer_shell_ = nullptr;
    virtual_keyboard_manager_ = nullptr;
    xdg_shell_ = nullptr;
    xdg_decoration_manager_ = nullptr;
  }
}

}  // namespace core
}  // namespace flakewm
