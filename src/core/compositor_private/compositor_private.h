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

#ifndef SRC_CORE_COMPOSITOR_PRIVATE_COMPOSITOR_PRIVATE_H_
#define SRC_CORE_COMPOSITOR_PRIVATE_COMPOSITOR_PRIVATE_H_

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "src/protocol/layer_shell/layer_surface.h"
#include "src/utils/args_handler/args_handler.h"
#include "src/utils/signal_listener.h"
#include "src/wlroots.h"

namespace flakewm {
namespace core {

class CompositorPrivate final {
  friend class LayerSurface;

 public:
  CompositorPrivate();
  ~CompositorPrivate();

  CompositorPrivate(const CompositorPrivate&) = delete;
  CompositorPrivate& operator=(const CompositorPrivate&) = delete;

  bool Start(const utils::StartupArgs& startup_args);
  void Run();
  void Stop();

 private:
  enum class CursorMode { kPassthrough, kMove, kResize };

  struct Output {
    Output(CompositorPrivate* compositor, wlr_output* output);

    bool CreateLayerTrees();
    wlr_scene_tree* LayerTree(uint32_t layer) const;
    void DestroyLayerTrees();

    static void OnFrame(Output* output, void*);
    static void OnRequestState(Output* output,
                               wlr_output_event_request_state* event);
    static void OnDestroy(Output* output, void*);

    CompositorPrivate* compositor;
    wlr_output* handle;
    wlr_scene_output* scene_output = nullptr;
    wlr_box usable_box = {};
    std::array<wlr_scene_tree*, 4> layer_trees = {};
    utils::SignalListener<Output, void> frame;
    utils::SignalListener<Output, wlr_output_event_request_state> request_state;
    utils::SignalListener<Output, void> destroy;
  };

  struct Keyboard {
    Keyboard(CompositorPrivate* compositor, wlr_input_device* device,
             wlr_keyboard* keyboard);

    static void OnModifiers(Keyboard* keyboard, void*);
    static void OnKey(Keyboard* keyboard, wlr_keyboard_key_event* event);
    static void OnDestroy(Keyboard* keyboard, void*);

    CompositorPrivate* compositor;
    wlr_input_device* device;
    wlr_keyboard* handle;
    utils::SignalListener<Keyboard, void> modifiers;
    utils::SignalListener<Keyboard, wlr_keyboard_key_event> key;
    utils::SignalListener<Keyboard, void> destroy;
  };

  struct Toplevel {
    Toplevel(CompositorPrivate* compositor, wlr_xdg_toplevel* toplevel);

    static void OnMap(Toplevel* toplevel, void*);
    static void OnUnmap(Toplevel* toplevel, void*);
    static void OnCommit(Toplevel* toplevel, void*);
    static void OnRequestMaximize(Toplevel* toplevel, void*);
    static void OnRequestMinimize(Toplevel* toplevel, void*);
    static void OnRequestFullscreen(Toplevel* toplevel, void*);
    static void OnRequestMove(Toplevel* toplevel, void*);
    static void OnRequestResize(Toplevel* toplevel,
                                wlr_xdg_toplevel_resize_event* event);
    static void OnDestroy(Toplevel* toplevel, void*);

    CompositorPrivate* compositor;
    wlr_xdg_toplevel* handle;
    wlr_scene_tree* scene_tree = nullptr;
    bool mapped = false;
    bool maximized = false;
    bool minimized = false;
    bool has_restore_box = false;
    bool restore_position_pending = false;
    wlr_box restore_box = {};
    wlr_box maximized_box = {};
    wlr_output* maximized_output = nullptr;
    utils::SignalListener<Toplevel, void> map;
    utils::SignalListener<Toplevel, void> unmap;
    utils::SignalListener<Toplevel, void> commit;
    utils::SignalListener<Toplevel, void> destroy;
    utils::SignalListener<Toplevel, void> request_move;
    utils::SignalListener<Toplevel, wlr_xdg_toplevel_resize_event>
        request_resize;
    utils::SignalListener<Toplevel, void> request_maximize;
    utils::SignalListener<Toplevel, void> request_minimize;
    utils::SignalListener<Toplevel, void> request_fullscreen;
  };

  struct Popup {
    Popup(CompositorPrivate* compositor, wlr_xdg_popup* popup);

    static void OnCommit(Popup* popup, void*);
    static void OnDestroy(Popup* popup, void*);

    CompositorPrivate* compositor;
    wlr_xdg_popup* handle;
    wlr_scene_tree* scene_tree = nullptr;
    utils::SignalListener<Popup, void> commit;
    utils::SignalListener<Popup, void> destroy;
  };

  void UpdateSeatCapabilities();
  void AddKeyboard(wlr_input_device* device);
  static void OnNewInput(CompositorPrivate* compositor,
                         wlr_input_device* device);
  Toplevel* ToplevelAt(double layout_x, double layout_y, wlr_surface** surface,
                       double* surface_x, double* surface_y) const;
  void FocusToplevel(Toplevel* toplevel);
  void FocusNextToplevel(Toplevel* excluding);
  void FocusLayerSurface(LayerSurface* layer_surface);
  LayerSurface* LayerSurfaceFor(wlr_surface* surface) const;
  Output* FindOutput(wlr_output* output) const;
  wlr_box OutputBoxAt(double layout_x, double layout_y) const;
  wlr_box UsableOutputBox(wlr_output* output) const;
  void ArrangeLayers(Output* output);
  void SetMaximized(Toplevel* toplevel, bool maximized);
  void ToggleMaximized(Toplevel* toplevel);
  void Minimize(Toplevel* toplevel);
  void RestoreForMove(Toplevel* toplevel);
  bool CursorAtOutputTop() const;
  bool IsTitlebarPoint(const Toplevel* toplevel, wlr_surface* surface,
                       double surface_y) const;
  void ResetCursorMode();
  void EndInteractive();
  void BeginInteractive(Toplevel* toplevel, CursorMode mode, uint32_t edges);
  void ProcessInteractiveMotion();
  void ProcessCursorMotion(uint32_t time_msec);
  static void OnCursorMotion(CompositorPrivate* compositor,
                             wlr_pointer_motion_event* event);
  static void OnCursorMotionAbsolute(CompositorPrivate* compositor,
                                     wlr_pointer_motion_absolute_event* event);
  static void OnCursorButton(CompositorPrivate* compositor,
                             wlr_pointer_button_event* event);
  static void OnCursorAxis(CompositorPrivate* compositor,
                           wlr_pointer_axis_event* event);
  static void OnCursorFrame(CompositorPrivate* compositor, void*);
  static void OnRequestCursor(CompositorPrivate* compositor,
                              wlr_seat_pointer_request_set_cursor_event* event);
  static void OnPointerFocusChange(CompositorPrivate* compositor,
                                   wlr_seat_pointer_focus_change_event* event);
  static void OnRequestSelection(CompositorPrivate* compositor,
                                 wlr_seat_request_set_selection_event* event);
  static void OnRequestPrimarySelection(
      CompositorPrivate* compositor,
      wlr_seat_request_set_primary_selection_event* event);
  static void OnNewOutput(CompositorPrivate* compositor, wlr_output* output);
  static void OnNewToplevel(CompositorPrivate* compositor,
                            wlr_xdg_toplevel* handle);
  static void OnNewPopup(CompositorPrivate* compositor, wlr_xdg_popup* handle);
  static void OnNewLayerSurface(CompositorPrivate* compositor,
                                wlr_layer_surface_v1* handle);
  static int OnTerminateSignal(int signal, void* data);

  bool ConfigureBackendEnvironment(
      const utils::StartupArgs& startup_args) const;
  bool Spawn(const std::string& command) const;
  bool Fail(const char* message) const;
  void Destroy();

  wl_display* display_ = nullptr;
  wlr_backend* backend_ = nullptr;
  wlr_renderer* renderer_ = nullptr;
  wlr_allocator* allocator_ = nullptr;
  wlr_output_layout* output_layout_ = nullptr;
  wlr_scene* scene_ = nullptr;
  wlr_scene_output_layout* scene_layout_ = nullptr;
  wlr_xdg_shell* xdg_shell_ = nullptr;
  wlr_layer_shell_v1* layer_shell_ = nullptr;
  wlr_seat* seat_ = nullptr;
  wlr_cursor* cursor_ = nullptr;
  wlr_xcursor_manager* cursor_manager_ = nullptr;
  std::array<wlr_scene_tree*, 4> shell_layer_trees_ = {};
  wlr_scene_tree* toplevel_tree_ = nullptr;

  utils::SignalListener<CompositorPrivate, wlr_output> new_output_{this,
                                                                   OnNewOutput};
  utils::SignalListener<CompositorPrivate, wlr_xdg_toplevel> new_toplevel_{
      this, OnNewToplevel};
  utils::SignalListener<CompositorPrivate, wlr_xdg_popup> new_popup_{
      this, OnNewPopup};
  utils::SignalListener<CompositorPrivate, wlr_layer_surface_v1>
      new_layer_surface_{this, OnNewLayerSurface};
  utils::SignalListener<CompositorPrivate, wlr_input_device> new_input_{
      this, OnNewInput};
  utils::SignalListener<CompositorPrivate, wlr_pointer_motion_event>
      cursor_motion_{this, OnCursorMotion};
  utils::SignalListener<CompositorPrivate, wlr_pointer_motion_absolute_event>
      cursor_motion_absolute_{this, OnCursorMotionAbsolute};
  utils::SignalListener<CompositorPrivate, wlr_pointer_button_event>
      cursor_button_{this, OnCursorButton};
  utils::SignalListener<CompositorPrivate, wlr_pointer_axis_event> cursor_axis_{
      this, OnCursorAxis};
  utils::SignalListener<CompositorPrivate, void> cursor_frame_{this,
                                                               OnCursorFrame};
  utils::SignalListener<CompositorPrivate,
                        wlr_seat_pointer_request_set_cursor_event>
      request_cursor_{this, OnRequestCursor};
  utils::SignalListener<CompositorPrivate, wlr_seat_pointer_focus_change_event>
      pointer_focus_change_{this, OnPointerFocusChange};
  utils::SignalListener<CompositorPrivate, wlr_seat_request_set_selection_event>
      request_selection_{this, OnRequestSelection};
  utils::SignalListener<CompositorPrivate,
                        wlr_seat_request_set_primary_selection_event>
      request_primary_selection_{this, OnRequestPrimarySelection};
  std::array<wl_event_source*, 2> signal_sources_ = {};

  std::vector<std::unique_ptr<Output>> outputs_;
  std::vector<std::unique_ptr<Keyboard>> keyboards_;
  std::vector<std::unique_ptr<Toplevel>> toplevels_;
  std::vector<std::unique_ptr<LayerSurface>> layer_surfaces_;
  std::vector<std::unique_ptr<Popup>> popups_;
  CursorMode cursor_mode_ = CursorMode::kPassthrough;
  Toplevel* grabbed_toplevel_ = nullptr;
  double grab_x_ = 0;
  double grab_y_ = 0;
  wlr_box grab_box_ = {};
  uint32_t resize_edges_ = 0;
  Toplevel* last_click_toplevel_ = nullptr;
  uint32_t last_click_time_msec_ = 0;
  double last_click_x_ = 0;
  double last_click_y_ = 0;
  bool suppress_button_release_ = false;
  bool maximize_on_release_ = false;
  std::string socket_name_;
  bool nested_ = false;
  bool started_ = false;
};

}  // namespace core
}  // namespace flakewm

#endif  // SRC_CORE_COMPOSITOR_PRIVATE_COMPOSITOR_PRIVATE_H_
