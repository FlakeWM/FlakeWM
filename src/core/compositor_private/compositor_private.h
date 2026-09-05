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

#include "src/backend/backend/backend.h"
#include "src/protocol/input_method/input_method_relay/input_method_relay.h"
#include "src/protocol/layer_shell/layer_surface.h"
#include "src/utils/args_handler/args_handler.h"
#include "src/utils/signal_listener.h"
#include "src/view/ssd/ssd/ssd.h"
#include "src/view/ssd/ssd_surface_clip/ssd_surface_clip.h"
#include "src/wlr_wrapper/wlroots.h"

namespace flakewm {
namespace xwayland {

class XSurface;
class XWaylandManager;

}  // namespace xwayland

namespace core {

class CompositorPrivate final {
  friend class LayerSurface;
  friend class xwayland::XSurface;
  friend class xwayland::XWaylandManager;

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

  struct XdgDecoration {
    XdgDecoration(CompositorPrivate* compositor,
                  wlr_xdg_toplevel_decoration_v1* decoration);

    void ApplyMode();
    static void OnRequestMode(XdgDecoration* decoration, void*);
    static void OnSurfaceCommit(XdgDecoration* decoration, void*);
    static void OnDestroy(XdgDecoration* decoration, void*);

    CompositorPrivate* compositor;
    wlr_xdg_toplevel_decoration_v1* handle;
    utils::SignalListener<XdgDecoration, void> request_mode{this,
                                                            OnRequestMode};
    utils::SignalListener<XdgDecoration, void> surface_commit{this,
                                                              OnSurfaceCommit};
    utils::SignalListener<XdgDecoration, void> destroy{this, OnDestroy};
  };

  struct Toplevel {
    Toplevel(CompositorPrivate* compositor, wlr_xdg_toplevel* toplevel);
    explicit Toplevel(CompositorPrivate* compositor);
    virtual ~Toplevel();

    virtual bool IsAlive() const;
    virtual bool IsXWayland() const;
    virtual bool WantsFocus() const;
    virtual bool CanManage() const;
    virtual bool CanMinimize() const;
    virtual bool CanMaximize() const;
    virtual bool RequestedMaximized() const;
    virtual bool RequestedFullscreen() const;
    virtual wlr_surface* Surface() const;
    virtual wlr_box Geometry() const;
    virtual void Configure(const wlr_box& box) const;
    virtual void SetActivated(bool activated) const;
    virtual void SetMaximizedState(bool maximized) const;
    virtual void SetMinimizedState(bool minimized) const;
    virtual void SetFullscreenState(bool fullscreen) const;
    virtual void Restack() const;
    wlr_box FrameGeometry() const;
    void UpdateCapabilities();

    static void OnMap(Toplevel* toplevel, void*);
    static void OnUnmap(Toplevel* toplevel, void*);
    static void OnCommit(Toplevel* toplevel, void*);
    static void OnRequestMaximize(Toplevel* toplevel, void*);
    static void OnRequestMinimize(Toplevel* toplevel, void*);
    static void OnRequestFullscreen(Toplevel* toplevel, void*);
    static void OnRequestMove(Toplevel* toplevel, void*);
    static void OnRequestResize(Toplevel* toplevel,
                                wlr_xdg_toplevel_resize_event* event);
    static void OnSetTitle(Toplevel* toplevel, void*);
    static void OnSetAppId(Toplevel* toplevel, void*);
    static void OnSetParent(Toplevel* toplevel, void*);
    static void OnDestroy(Toplevel* toplevel, void*);

    CompositorPrivate* compositor;
    wlr_xdg_toplevel* handle = nullptr;
    wlr_scene_tree* scene_tree = nullptr;
    bool mapped = false;
    bool maximized = false;
    bool minimized = false;
    bool has_restore_box = false;
    bool restore_position_pending = false;
    bool capabilities_advertised = false;
    uint32_t advertised_capabilities = 0;
    wlr_box restore_box = {};
    wlr_box maximized_box = {};
    wlr_output* maximized_output = nullptr;
    std::unique_ptr<view::Ssd> ssd;
    std::unique_ptr<view::SsdSurfaceClip> ssd_clip;
    bool ssd_initial_position_pending = false;
    utils::SignalListener<Toplevel, void> map{this, OnMap};
    utils::SignalListener<Toplevel, void> unmap{this, OnUnmap};
    utils::SignalListener<Toplevel, void> commit{this, OnCommit};
    utils::SignalListener<Toplevel, void> destroy{this, OnDestroy};
    utils::SignalListener<Toplevel, void> request_move{this, OnRequestMove};
    utils::SignalListener<Toplevel, wlr_xdg_toplevel_resize_event>
        request_resize{this, OnRequestResize};
    utils::SignalListener<Toplevel, void> request_maximize{this,
                                                           OnRequestMaximize};
    utils::SignalListener<Toplevel, void> request_minimize{this,
                                                           OnRequestMinimize};
    utils::SignalListener<Toplevel, void> request_fullscreen{
        this, OnRequestFullscreen};
    utils::SignalListener<Toplevel, void> set_title{this, OnSetTitle};
    utils::SignalListener<Toplevel, void> set_app_id{this, OnSetAppId};
    utils::SignalListener<Toplevel, void> set_parent{this, OnSetParent};
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
  static void OnNewVirtualKeyboard(CompositorPrivate* compositor,
                                   wlr_virtual_keyboard_v1* keyboard);
  Toplevel* ToplevelAt(double layout_x, double layout_y, wlr_surface** surface,
                       double* surface_x, double* surface_y) const;
  Toplevel* FindToplevel(wlr_xdg_toplevel* handle) const;
  void AttachSsd(Toplevel* toplevel);
  view::Ssd::HitTarget SsdHitAt(const Toplevel* toplevel) const;
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
  static void OnNewXdgDecoration(CompositorPrivate* compositor,
                                 wlr_xdg_toplevel_decoration_v1* decoration);
  static void OnNewPopup(CompositorPrivate* compositor, wlr_xdg_popup* handle);
  static void OnNewLayerSurface(CompositorPrivate* compositor,
                                wlr_layer_surface_v1* handle);
  static int OnTerminateSignal(int signal, void* data);
  static int OnQtFrameTimer(void* data);

  bool ConfigureBackendEnvironment(
      const utils::StartupArgs& startup_args) const;
  void UpdateQtFrameInterval();
  bool Spawn(const std::string& command) const;
  bool Fail(const char* message) const;
  void Destroy();

  wl_display* display_ = nullptr;
  std::unique_ptr<backend::Backend> backend_owner_;
  wlr_backend* backend_ = nullptr;
  wlr_renderer* renderer_ = nullptr;
  wlr_allocator* allocator_ = nullptr;
  wlr_compositor* compositor_ = nullptr;
  wlr_output_layout* output_layout_ = nullptr;
  wlr_scene* scene_ = nullptr;
  wlr_scene_output_layout* scene_layout_ = nullptr;
  wlr_xdg_shell* xdg_shell_ = nullptr;
  wlr_xdg_decoration_manager_v1* xdg_decoration_manager_ = nullptr;
  wlr_layer_shell_v1* layer_shell_ = nullptr;
  wlr_virtual_keyboard_manager_v1* virtual_keyboard_manager_ = nullptr;
  std::unique_ptr<protocol::InputMethodRelay> input_method_relay_;
  std::unique_ptr<xwayland::XWaylandManager> xwayland_;
  wlr_seat* seat_ = nullptr;
  wlr_cursor* cursor_ = nullptr;
  wlr_xcursor_manager* cursor_manager_ = nullptr;
  std::array<wlr_scene_tree*, 4> shell_layer_trees_ = {};
  wlr_scene_tree* toplevel_tree_ = nullptr;

  utils::SignalListener<CompositorPrivate, wlr_output> new_output_{this,
                                                                   OnNewOutput};
  utils::SignalListener<CompositorPrivate, wlr_xdg_toplevel> new_toplevel_{
      this, OnNewToplevel};
  utils::SignalListener<CompositorPrivate, wlr_xdg_toplevel_decoration_v1>
      new_xdg_decoration_{this, OnNewXdgDecoration};
  utils::SignalListener<CompositorPrivate, wlr_xdg_popup> new_popup_{
      this, OnNewPopup};
  utils::SignalListener<CompositorPrivate, wlr_layer_surface_v1>
      new_layer_surface_{this, OnNewLayerSurface};
  utils::SignalListener<CompositorPrivate, wlr_input_device> new_input_{
      this, OnNewInput};
  utils::SignalListener<CompositorPrivate, wlr_virtual_keyboard_v1>
      new_virtual_keyboard_{this, OnNewVirtualKeyboard};
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
  wl_event_source* qt_frame_timer_ = nullptr;
  int qt_frame_interval_ms_ = 16;

  std::vector<std::unique_ptr<Output>> outputs_;
  std::vector<std::unique_ptr<Keyboard>> keyboards_;
  std::vector<std::unique_ptr<XdgDecoration>> xdg_decorations_;
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
