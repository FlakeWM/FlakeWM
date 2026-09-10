# protocol
> https://wayland.app/protocols/

「支持情况」中的数字表示 FlakeWM 发布的最高接口版本，`no` 表示当前尚未支持。「说明」中的数字为待支持的协议版本。


## Wayland core

> https://gitlab.freedesktop.org/wayland/wayland.git

| 协议名称               | 支持情况 | 说明                     |
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

| 协议名称        | 支持情况 | 说明 |
| :-------------- | :------: | :--- |
| wp_presentation |    2     |      |
| wp_viewporter   |    1     |      |
| xdg_wm_base     |    5     |      |

### staging

| 协议名称                          | 支持情况 | 说明                         |
| :-------------------------------- | :------: | :--------------------------- |
| wp_color_manager_v1               |    2     | 依渲染器能力提供颜色/HDR 元数据 |
| wp_content_type_manager_v1        |    no    | 1                            |
| wp_cursor_shape_manager_v1        |    no    | 1                            |
| wp_drm_lease_device_v1            |    no    | 1                            |
| ext_foreign_toplevel_list_v1      |    1     | Portal 窗口选择              |
| ext_foreign_toplevel_image_capture_source_manager_v1 | 1 | Portal 窗口捕获源 |
| ext_image_copy_capture_manager_v1 |    1     | Portal/PipeWire 捕获         |
| ext_idle_notifier_v1              |    2     |                              |
| ext_output_image_capture_source_manager_v1 | 1 | Portal 输出捕获源 |
| ext_session_lock_manager_v1       |    1     | 安全失败关闭锁屏             |
| ext_transient_seat_manager_v1     |    1     |                              |
| wp_fractional_scale_manager_v1    |    1     |                              |
| wp_linux_drm_syncobj_manager_v1   |    1     | 渲染器和后端支持 timeline 时发布 |
| wp_security_context_manager_v1    |    1     | 沙箱客户端 global 过滤        |
| wp_single_pixel_buffer_manager_v1 |    no    | 1                            |
| wp_tearing_control_manager_v1     |    1     |                              |
| xdg_activation_v1                 |    1     |                              |
| xdg_wm_dialog_v1                  |    1     |                              |
| xwayland_shell_v1                 |    1     | XWayland 成功启动时发布      |
| xdg_toplevel_drag_manager_v1      |    1     |                              |

### unstable

| 协议名称                                  | 支持情况 | 说明                              |
| :---------------------------------------- | :------: | :-------------------------------- |
| zwp_fullscreen_shell_v1                   |    no    | 1                                 |
| zwp_idle_inhibit_manager_v1               |    1     |                                   |
| zwp_input_method_context_v1               |    no    | 1                                 |
| zwp_input_timestamps_manager_v1           |    1     |                                   |
| zwp_keyboard_shortcuts_inhibit_manager_v1 |    1     |                                   |
| zwp_linux_dmabuf_v1                       |    4     | 渲染器支持 DMA-BUF 时发布         |
| zwp_linux_explicit_synchronization_v1     |    no    | 旧协议，改用 linux-drm-syncobj-v1 |
| zwp_pointer_constraints_v1                |    1     |                                   |
| zwp_pointer_gestures_v1                   |    3     |                                   |
| zwp_primary_selection_device_manager_v1   |    1     |                                   |
| zwp_relative_pointer_manager_v1           |    1     | 为锁定指针提供原始移动量          |
| zwp_tablet_manager_v1                     |    no    | deprecated，使用 tablet v2        |
| zwp_tablet_manager_v2                     |    1     |                                   |
| zwp_text_input_v1                         |    1     |                                   |
| zwp_text_input_v3                         |    1     |                                   |
| zxdg_decoration_manager_v1                |    1     |                                   |
| zxdg_exporter_v1                          |    no    | 1                                 |
| zxdg_exporter_v2                          |    no    | 1                                 |
| zxdg_output_manager_v1                    |    3     |                                   |
| xdg_shell                                 |    no    | deprecated，使用 xdg_wm_base      |
| zxdg_shell_v6                             |    no    | deprecated，使用 xdg_wm_base      |
| zwp_xwayland_keyboard_grab_manager_v1     |    no    | 1                                 |

## wlr

> https://gitlab.freedesktop.org/wlroots/wlroots.git

| 协议名称                         | 支持情况 | 说明       |
| :------------------------------- | :------: | :--------- |
| zwlr_data_control_manager_v1     |    2     |            |
| zwlr_export_dmabuf_manager_v1    |    1     |            |
| zwlr_foreign_toplevel_manager_v1 |    3     |            |
| zwlr_gamma_control_manager_v1    |    1     | 对 security-context 客户端隐藏 |
| zwlr_input_inhibit_manager_v1    |    no    | deprecated |
| zwlr_layer_shell_v1              |    4     |            |
| zwlr_output_manager_v1           |    4     |            |
| zwlr_output_power_manager_v1     |    1     |            |
| zwlr_screencopy_manager_v1       |    3     |            |
| zwlr_virtual_pointer_v1          |    2     |            |

security-context 客户端只能看到经过审核的普通应用协议。屏幕捕获、虚拟输入、
输出控制、会话锁、输入法、Layer Shell、窗口枚举、Data Control 和 Gamma
Control 等敏感 global 均被隐藏；通过合成器常规 socket 连接的可信桌面服务仍可
使用完整协议集合。

## kde

> https://invent.kde.org/libraries/plasma-wayland-protocols.git

| 协议名称                                       | 支持情况 | 说明       |
| :--------------------------------------------- | :------: | :--------- |
| org_kde_kwin_appmenu_manager                   |    no    | 2          |
| org_kde_kwin_blur_manager                      |    no    | 1，ukui    |
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
| org_kde_kwin_outputmanagement                  |    no    | 4，使用 v2 |
| org_kde_kwin_outputdevice                      |    no    | 4，使用 v2 |
| org_kde_plasma_shell                           |    no    | 6，ukui    |
| org_kde_plasma_window_management               |    no    | 16         |
| org_kde_kwin_remote_access_manager             |    no    | 1          |
| org_kde_kwin_server_decoration_palette_manager |    no    | 1          |
| org_kde_kwin_server_decoration_manager         |    no    | 1          |
| org_kde_kwin_shadow_manager                    |    no    | 2          |
| org_kde_kwin_slide_manager                     |    no    | 1          |
| zkde_screencast_unstable_v1                    |    no    | 3          |

## weston

> https://gitlab.freedesktop.org/wayland/weston.git

| 协议名称                  | 支持情况 | 说明 |
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

| 协议名称                | 支持情况 | 说明 |
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

| 协议名称             | 支持情况 | 说明 |
| :------------------- | :------: | :--- |
| gxde-identifier-v1   |    no    | 1    |
| gxde-screenshot-v1   |    no    | 1    |

## ukui

> https://gitee.com/openkylin/kylin-wayland-protocols

| 协议名称                  | 支持情况 | 说明 |
| :------------------------ | :------: | :--- |
| ukui_blur_manager_v1      |    no    | 1    |
| ukui_output_management_v1 |    no    | 1    |
| ukui_shell                |    no    | 3    |
| ukui_window_management    |    no    | 1    |

## treeland

> https://github.com/linuxdeepin/treeland-protocols

| 协议名称                           | 支持情况 | 说明                                                                                                                    |
| :--------------------------------- | :------: | :---------------------------------------------------------------------------------------------------------------------- |
| treeland_dde_shell_manager_v1      |    no    | 1                                                                                                                       |
| treeland_app_id_resolver_v1        |    no    | 1                                                                                                                       |
| treeland_capture_manager_v1        |    no    | 1；供 deepin-screen-recorder 录屏与点击窗口截图使用                                                                     |
| treeland_personalization_manager_v1 |    no    | 1；实现时需兼容 Treeland Protocol 0.5.8 和 0.5.9 中签名不同的同名 v1 XML                                              |
