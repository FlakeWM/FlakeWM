# Protocol
> https://wayland.app/protocols/

The number under the 「Version Supported」 section stands for the highest interface version that FlakeWM broadcasts, while a `no` means that current protocol is NOT supported yet; The number under the 「Note」 section stands for the version of the protocol that needs to be inplemented.

## Wayland core

> https://gitlab.freedesktop.org/wayland/wayland.git

| Name               | Version Supported | Note                     |
| :--------------------- | :------: | :----------------------- |
| wl_compositor          |    6     |                          |
| wl_shm                 |    2     |                          |
| wl_data_device_manager |    3     |                          |
| wl_shell               |    no    | deprecated               |
| wl_seat                |    9     |                          |
| wl_output              |    4     |                          |
| wl_subcompositor       |    1     |                          |

## Wayland protocols

> https://gitlab.freedesktop.org/wayland/wayland-protocols.git

### stable

| Name        | Version Supported | Note |
| :-------------- | :------: | :--- |
| wp_presentation |    2     |      |
| wp_viewporter   |    1     |      |
| xdg_wm_base     |    5     |      |

### staging

| Name                          | Version Supported | Note                         |
| :-------------------------------- | :------: | :--------------------------- |
| wp_color_manager_v1               |    2     | Renderer-dependent color/HDR metadata |
| wp_content_type_manager_v1        |    no    | 1                            |
| wp_cursor_shape_manager_v1        |    no    | 1                            |
| wp_drm_lease_device_v1            |    no    | 1                            |
| ext_foreign_toplevel_list_v1      |    1     | Portal window selection      |
| ext_foreign_toplevel_image_capture_source_manager_v1 | 1 | Portal window capture source |
| ext_image_copy_capture_manager_v1 |    1     | Portal/PipeWire capture      |
| ext_idle_notifier_v1              |    2     |                              |
| ext_output_image_capture_source_manager_v1 | 1 | Portal output capture source |
| ext_session_lock_manager_v1       |    1     | Secure fail-closed lock      |
| ext_transient_seat_manager_v1     |    1     |                              |
| wp_fractional_scale_manager_v1    |    1     |                              |
| wp_linux_drm_syncobj_manager_v1   |    1     | Published with renderer/backend timeline support |
| wp_security_context_manager_v1    |    1     | Sandboxed-client global filtering |
| wp_single_pixel_buffer_manager_v1 |    no    | 1                            |
| wp_tearing_control_manager_v1     |    1     |                              |
| xdg_activation_v1                 |    1     |                              |
| xdg_wm_dialog_v1                  |    1     |                              |
| xwayland_shell_v1                 |    1     | Broadcasts once XWayland is avaliable      |
| xdg_toplevel_drag_manager_v1      |    1     |                              |

### unstable

| Name                                  | Version Supported | Note                              |
| :---------------------------------------- | :------: | :-------------------------------- |
| zwp_fullscreen_shell_v1                   |    no    | 1                                 |
| zwp_idle_inhibit_manager_v1               |    1     |                                   |
| zwp_input_method_context_v1               |    no    | 1                                 |
| zwp_input_timestamps_manager_v1           |    1     |                                   |
| zwp_keyboard_shortcuts_inhibit_manager_v1 |    1     |                                   |
| zwp_linux_dmabuf_v1                       |    4     | Baoadcasts when render supports DMA-BUF |
| zwp_linux_explicit_synchronization_v1     |    no    | Legacy; use linux-drm-syncobj-v1  |
| zwp_pointer_constraints_v1                |    1     |                                   |
| zwp_pointer_gestures_v1                   |    3     |                                   |
| zwp_primary_selection_device_manager_v1   |    1     |                                   |
| zwp_relative_pointer_manager_v1           |    1     | Raw motion for locked pointers    |
| zwp_tablet_manager_v1                     |    no    | deprecated, using tablet v2 instead        |
| zwp_tablet_manager_v2                     |    1     |                                   |
| zwp_text_input_v1                         |    1     |                                   |
| zwp_text_input_v3                         |    1     |                                   |
| zxdg_decoration_manager_v1                |    1     |                                   |
| zxdg_exporter_v1                          |    no    | 1                                 |
| zxdg_exporter_v2                          |    no    | 1                                 |
| zxdg_output_manager_v1                    |    3     |                                   |
| xdg_shell                                 |    no    | deprecated, use xdg_wm_base instead |
| zxdg_shell_v6                             |    no    | deprecated, use xdg_wm_base instead|
| zwp_xwayland_keyboard_grab_manager_v1     |    no    | 1                                 |

## wlr

> https://gitlab.freedesktop.org/wlroots/wlroots.git

| Name                         | Version Supported | Note       |
| :------------------------------- | :------: | :--------- |
| zwlr_data_control_manager_v1     |    2     |            |
| zwlr_export_dmabuf_manager_v1    |    1     |            |
| zwlr_foreign_toplevel_manager_v1 |    3     |            |
| zwlr_gamma_control_manager_v1    |    1     | Hidden from security-context clients |
| zwlr_input_inhibit_manager_v1    |    no    | deprecated |
| zwlr_layer_shell_v1              |    4     |            |
| zwlr_output_manager_v1           |    4     |            |
| zwlr_output_power_manager_v1     |    1     |            |
| zwlr_screencopy_manager_v1       |    3     |            |
| zwlr_virtual_pointer_v1          |    2     |            |

Security-context clients receive an allow-list of ordinary application
protocols. Screen capture, virtual input, output control, session lock, input
method, layer-shell, foreign-toplevel enumeration, data-control and Gamma
Control globals are hidden. Trusted desktop services using the compositor's
regular socket retain the complete registry.

## kde

> https://invent.kde.org/libraries/plasma-wayland-protocols.git

| Name                                       | Version Supported | Note       |
| :--------------------------------------------- | :------: | :--------- |
| org_kde_kwin_appmenu_manager                   |    no    | 2          |
| org_kde_kwin_blur_manager                      |    no    | 1, ukui    |
| org_kde_kwin_contrast_manager                  |    no    | 2          |
| org_kde_kwin_dpms_manager                      |    no    | 1          |
| org_kde_kwin_fake_input                        |    no    | 5          |
| org_kde_kwin_idle                              |    no    | 1          |
| kde_lockscreen_overlay_v1                      |    no    | 1          |
| kde_output_device_v2                           |    no    | 6          |
| kde_output_management_v2                       |    no    | 7          |
| kde_output_order_v1                            |    no    | 1          |
| kde_primary_output_v1                          |    no    | 2          |
| kde_screen_edge_manager_v1                     |    no    | 1          |
| org_kde_kwin_keystate                          |    no    | 4          |
| org_kde_plasma_virtual_desktop_management      |    no    | 2          |
| org_kde_kwin_outputmanagement                  |    no    | 4, use v2 |
| org_kde_kwin_outputdevice                      |    no    | 4, use v2 |
| org_kde_plasma_shell                           |    no    | 6, ukui    |
| org_kde_plasma_window_management               |    no    | 16         |
| org_kde_kwin_remote_access_manager             |    no    | 1          |
| org_kde_kwin_server_decoration_palette_manager |    no    | 1          |
| org_kde_kwin_server_decoration_manager         |    no    | 1          |
| org_kde_kwin_shadow_manager                    |    no    | 2          |
| org_kde_kwin_slide_manager                     |    no    | 1          |
| zkde_screencast_unstable_v1                    |    no    | 3          |

## weston

> https://gitlab.freedesktop.org/wayland/weston.git

| Name                  | Version Supported | Note |
| :------------------------ | :------: | :--- |
| ivi_surface               |    no    | 1    |
| ivi_hmi_controller        |    no    | 1    |
| text_cursor_position      |    no    | 1    |
| weston_content_protection |    no    | 1    |
| weston_debug_v1           |    no    | 1    |
| weston_desktop_shell      |    no    | 1    |
| weston_direct_display_v1  |    no    | 1    |
| weston_capture_v1         |    no    | 1    |
| weston_test               |    no    | 1    |
| weston_touch_calibration  |    no    | 1    |

## external

| Name                | Version Supported | Note |
| :---------------------- | :------: | :--- |
| wl_drm                  |    no    | 2    |
| wl_eglstream            |    no    | 1    |
| wl_eglstream_controller |    no    | 2    |
| zwp_input_method_v2     |    1     |      |
| zwp_virtual_keyboard_v1 |    1     |      |
| zwp_text_input_v2       |    1     |      |
| qt_surface_extension    |    no    | 1    |
| gtk_shell1              |    no    | 5    |

## gxwm

| Name             | Version Supported | Note |
| :------------------- | :------: | :--- |
| gxde-identifier-v1   |    no    | 1    |
| gxde-screenshot-v1   |    no    | 1    |

## ukui

> https://gitee.com/openkylin/kylin-wayland-protocols

| Name                  | Version Supported | Note |
| :------------------------ | :------: | :--- |
| ukui_blur_manager_v1      |    no    | 1    |
| ukui_output_management_v1 |    no    | 1    |
| ukui_shell                |    no    | 3    |
| ukui_window_management    |    no    | 1    |

## treeland

> https://github.com/linuxdeepin/treeland-protocols

| Name                           | Version Supported | Note                                                                                                                    |
| :--------------------------------- | :------: | :---------------------------------------------------------------------------------------------------------------------- |
| treeland_dde_shell_manager_v1      |    no    | 1                                                                                                                       |
| treeland_app_id_resolver_v1        |    no    | 1                                                                                                                       |
| treeland_capture_manager_v1        |    no    | 1; For deepin-screen-recorder ONLY.                                                                     |
| treeland_personalization_manager_v1 |    no    | 1; Watch out for signature mismatch (while version NOT bumped) for protocol 0.5.8 & 0.5.9 in XML.                                              |
