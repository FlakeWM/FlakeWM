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

#ifndef SRC_PROTOCOL_PROTOCOL_MANAGER_PROTOCOL_MANAGER_H_
#define SRC_PROTOCOL_PROTOCOL_MANAGER_PROTOCOL_MANAGER_H_

#include <memory>
#include <unordered_map>
#include <vector>

#include "src/input/touchpad_manager.h"
#include "src/protocol/foreign_toplevel/foreign_toplevel.h"
#include "src/protocol/input_timestamps/input_timestamps_manager.h"
#include "src/protocol/toplevel_drag/toplevel_drag_manager.h"
#include "src/utils/signal_listener.h"
#include "src/wlr_wrapper/wlr_layer_shell.h"
#include "src/wlr_wrapper/wlroots.h"

namespace flakewm {
namespace core {
class CompositorPrivate;
}  // namespace core
}  // namespace flakewm

namespace flakewm {
namespace protocol {

class ProtocolManager final {
 public:
  explicit ProtocolManager(core::CompositorPrivate* compositor);
  ~ProtocolManager();

  ProtocolManager(const ProtocolManager&) = delete;
  ProtocolManager& operator=(const ProtocolManager&) = delete;

  bool Create(wl_display* display, wlr_backend* backend, wlr_seat* seat,
              wlr_cursor* cursor, wlr_scene_tree* drag_icon_parent,
              wlr_output_layout* output_layout);
  void AddInput(wlr_input_device* device);
  void AddOutput(wlr_output* output);
  void UpdateOutputs();
  void NotifyKeyboard(uint32_t time_msec);
  void NotifyPointer(uint32_t time_msec);
  void NotifyTouch(wlr_surface* surface, uint32_t time_msec);
  bool ShouldForwardAxis(const wlr_pointer_axis_event& event) const;
  bool WantsTearing(wlr_surface* surface) const;
  bool ShortcutsInhibited() const;
  bool ConfinePointer(double* delta_x, double* delta_y) const;
  void UpdatePointerFocus(wlr_surface* surface);
  void UpdateKeyboardFocus(wlr_surface* surface);
  void MoveDrag();

  void MapToplevel(wlr_surface* surface, const char* title, const char* app_id);
  void UnmapToplevel(wlr_surface* surface);
  void UpdateToplevel(wlr_surface* surface);
  void UpdateToplevelParent(wlr_surface* surface, wlr_surface* parent);

  void RequestMaximize(wlr_surface* surface, bool maximized);
  void RequestMinimize(wlr_surface* surface, bool minimized);
  void RequestActivate(wlr_surface* surface, wlr_seat* seat);
  void RequestFullscreen(wlr_surface* surface, bool fullscreen,
                         wlr_output* output);
  void RequestClose(wlr_surface* surface);

 private:
  static void OnNewVirtualPointer(
      ProtocolManager* manager,
      wlr_virtual_pointer_v1_new_pointer_event* event);
  static void OnNewConstraint(ProtocolManager* manager,
                              wlr_pointer_constraint_v1* constraint);
  static void OnConstraintDestroy(ProtocolManager* manager, void*);
  static void OnNewIdleInhibitor(ProtocolManager* manager,
                                 wlr_idle_inhibitor_v1* inhibitor);
  static void OnNewShortcutsInhibitor(
      ProtocolManager* manager, wlr_keyboard_shortcuts_inhibitor_v1* inhibitor);
  static void OnCreateTransientSeat(ProtocolManager* manager,
                                    wlr_transient_seat_v1* transient_seat);
  static void OnRequestActivate(
      ProtocolManager* manager,
      wlr_xdg_activation_v1_request_activate_event* event);
  static void OnOutputApply(ProtocolManager* manager,
                            wlr_output_configuration_v1* configuration);
  static void OnOutputTest(ProtocolManager* manager,
                           wlr_output_configuration_v1* configuration);
  static void OnOutputPower(ProtocolManager* manager,
                            wlr_output_power_v1_set_mode_event* event);
  static void OnRequestStartDrag(ProtocolManager* manager,
                                 wlr_seat_request_start_drag_event* event);
  static void OnStartDrag(ProtocolManager* manager, wlr_drag* drag);
  static void OnDragDestroy(ProtocolManager* manager, void*);
  static void OnSwipeBegin(ProtocolManager* manager,
                           wlr_pointer_swipe_begin_event* event);
  static void OnSwipeUpdate(ProtocolManager* manager,
                            wlr_pointer_swipe_update_event* event);
  static void OnSwipeEnd(ProtocolManager* manager,
                         wlr_pointer_swipe_end_event* event);
  static void OnPinchBegin(ProtocolManager* manager,
                           wlr_pointer_pinch_begin_event* event);
  static void OnPinchUpdate(ProtocolManager* manager,
                            wlr_pointer_pinch_update_event* event);
  static void OnPinchEnd(ProtocolManager* manager,
                         wlr_pointer_pinch_end_event* event);
  static void OnHoldBegin(ProtocolManager* manager,
                          wlr_pointer_hold_begin_event* event);
  static void OnHoldEnd(ProtocolManager* manager,
                        wlr_pointer_hold_end_event* event);
  static void OnTabletAxis(ProtocolManager* manager,
                           wlr_tablet_tool_axis_event* event);
  static void OnTabletProximity(ProtocolManager* manager,
                                wlr_tablet_tool_proximity_event* event);
  static void OnTabletTip(ProtocolManager* manager,
                          wlr_tablet_tool_tip_event* event);
  static void OnTabletButton(ProtocolManager* manager,
                             wlr_tablet_tool_button_event* event);

  void ApplyOutputConfiguration(wlr_output_configuration_v1* configuration,
                                bool test_only);
  void ActivateConstraint(wlr_pointer_constraint_v1* constraint);
  void UpdateIdleInhibition();
  ForeignToplevel* FindForeign(wlr_surface* surface) const;
  wlr_tablet_v2_tablet_tool* TabletTool(wlr_tablet_tool* tool);
  void TabletMotion(wlr_tablet_tool* tool, double x, double y);

  core::CompositorPrivate* compositor_;
  wlr_backend* backend_ = nullptr;
  wlr_seat* seat_ = nullptr;
  wlr_cursor* cursor_ = nullptr;
  wlr_scene_tree* drag_icon_parent_ = nullptr;
  wlr_scene_tree* drag_icon_tree_ = nullptr;
  wlr_output_layout* output_layout_ = nullptr;
  wlr_idle_notifier_v1* idle_notifier_ = nullptr;
  wlr_idle_inhibit_manager_v1* idle_inhibit_manager_ = nullptr;
  wlr_keyboard_shortcuts_inhibit_manager_v1* shortcuts_manager_ = nullptr;
  wlr_pointer_constraints_v1* pointer_constraints_ = nullptr;
  wlr_pointer_gestures_v1* pointer_gestures_ = nullptr;
  wlr_tablet_manager_v2* tablet_manager_ = nullptr;
  wlr_virtual_pointer_manager_v1* virtual_pointer_manager_ = nullptr;
  wlr_transient_seat_manager_v1* transient_seat_manager_ = nullptr;
  wlr_tearing_control_manager_v1* tearing_manager_ = nullptr;
  wlr_xdg_activation_v1* activation_manager_ = nullptr;
  wlr_foreign_toplevel_manager_v1* foreign_manager_ = nullptr;
  wlr_output_manager_v1* output_manager_ = nullptr;
  wlr_output_power_manager_v1* output_power_manager_ = nullptr;
  wlr_pointer_constraint_v1* active_constraint_ = nullptr;
  std::unique_ptr<InputTimestampsManager> input_timestamps_;
  std::unique_ptr<ToplevelDragManager> toplevel_drag_manager_;
  std::unique_ptr<input::TouchpadManager> touchpad_manager_;
  std::vector<std::unique_ptr<ForeignToplevel>> foreign_toplevels_;
  std::unordered_map<wlr_tablet*, wlr_tablet_v2_tablet*> tablets_;
  std::unordered_map<wlr_tablet_tool*, wlr_tablet_v2_tablet_tool*>
      tablet_tools_;

  utils::SignalListener<ProtocolManager,
                        wlr_virtual_pointer_v1_new_pointer_event>
      new_virtual_pointer_{this, OnNewVirtualPointer};
  utils::SignalListener<ProtocolManager, wlr_pointer_constraint_v1>
      new_constraint_{this, OnNewConstraint};
  utils::SignalListener<ProtocolManager, void> constraint_destroy_{
      this, OnConstraintDestroy};
  utils::SignalListener<ProtocolManager, wlr_idle_inhibitor_v1>
      new_idle_inhibitor_{this, OnNewIdleInhibitor};
  utils::SignalListener<ProtocolManager, wlr_keyboard_shortcuts_inhibitor_v1>
      new_shortcuts_inhibitor_{this, OnNewShortcutsInhibitor};
  utils::SignalListener<ProtocolManager, wlr_transient_seat_v1>
      create_transient_seat_{this, OnCreateTransientSeat};
  utils::SignalListener<ProtocolManager,
                        wlr_xdg_activation_v1_request_activate_event>
      request_activate_{this, OnRequestActivate};
  utils::SignalListener<ProtocolManager, wlr_output_configuration_v1>
      output_apply_{this, OnOutputApply};
  utils::SignalListener<ProtocolManager, wlr_output_configuration_v1>
      output_test_{this, OnOutputTest};
  utils::SignalListener<ProtocolManager, wlr_output_power_v1_set_mode_event>
      output_power_{this, OnOutputPower};
  utils::SignalListener<ProtocolManager, wlr_seat_request_start_drag_event>
      request_start_drag_{this, OnRequestStartDrag};
  utils::SignalListener<ProtocolManager, wlr_drag> start_drag_{this,
                                                               OnStartDrag};
  utils::SignalListener<ProtocolManager, void> drag_destroy_{this,
                                                             OnDragDestroy};
  utils::SignalListener<ProtocolManager, wlr_pointer_swipe_begin_event>
      swipe_begin_{this, OnSwipeBegin};
  utils::SignalListener<ProtocolManager, wlr_pointer_swipe_update_event>
      swipe_update_{this, OnSwipeUpdate};
  utils::SignalListener<ProtocolManager, wlr_pointer_swipe_end_event>
      swipe_end_{this, OnSwipeEnd};
  utils::SignalListener<ProtocolManager, wlr_pointer_pinch_begin_event>
      pinch_begin_{this, OnPinchBegin};
  utils::SignalListener<ProtocolManager, wlr_pointer_pinch_update_event>
      pinch_update_{this, OnPinchUpdate};
  utils::SignalListener<ProtocolManager, wlr_pointer_pinch_end_event>
      pinch_end_{this, OnPinchEnd};
  utils::SignalListener<ProtocolManager, wlr_pointer_hold_begin_event>
      hold_begin_{this, OnHoldBegin};
  utils::SignalListener<ProtocolManager, wlr_pointer_hold_end_event> hold_end_{
      this, OnHoldEnd};
  utils::SignalListener<ProtocolManager, wlr_tablet_tool_axis_event>
      tablet_axis_{this, OnTabletAxis};
  utils::SignalListener<ProtocolManager, wlr_tablet_tool_proximity_event>
      tablet_proximity_{this, OnTabletProximity};
  utils::SignalListener<ProtocolManager, wlr_tablet_tool_tip_event> tablet_tip_{
      this, OnTabletTip};
  utils::SignalListener<ProtocolManager, wlr_tablet_tool_button_event>
      tablet_button_{this, OnTabletButton};
};

}  // namespace protocol
}  // namespace flakewm

#endif  // SRC_PROTOCOL_PROTOCOL_MANAGER_PROTOCOL_MANAGER_H_
