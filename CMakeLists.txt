# Copyright (C) 2026 CharOfString <root@charofstring.cc>
#
# This program is free software: you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
# more details.
#
# You should have received a copy of the GNU General Public License along with
# this program. If not, see <https://www.gnu.org/licenses/>.

cmake_minimum_required(VERSION 3.21)

project(flakewm VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(ABSL_BUILD_TESTING OFF CACHE BOOL "Build Abseil tests" FORCE)
set(ABSL_BUILD_TEST_HELPERS OFF CACHE BOOL "Build Abseil test helpers" FORCE)
set(ABSL_ENABLE_INSTALL OFF CACHE BOOL "Install Abseil" FORCE)
set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "Use the parent C++ standard" FORCE)

set(_flakewm_build_shared_libs_was_defined FALSE)
if(DEFINED BUILD_SHARED_LIBS)
  set(_flakewm_build_shared_libs_was_defined TRUE)
  set(_flakewm_saved_build_shared_libs "${BUILD_SHARED_LIBS}")
endif()

set(BUILD_SHARED_LIBS OFF)
add_subdirectory(libs/abseil-cpp EXCLUDE_FROM_ALL)

if(_flakewm_build_shared_libs_was_defined)
  set(BUILD_SHARED_LIBS "${_flakewm_saved_build_shared_libs}")
else()
  unset(BUILD_SHARED_LIBS)
endif()

include(cmake/wlroots.cmake)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core DBus Gui Qml Quick)
find_path(QGSETTINGS_QT6_INCLUDE_DIR NAMES QGSettings/qgsettings.h
  PATH_SUFFIXES qt6
)
find_library(QGSETTINGS_QT6_LIBRARY NAMES gsettings-qt6)
find_program(XWAYLAND_EXECUTABLE NAMES Xwayland REQUIRED)
find_program(WAYLAND_SCANNER_EXECUTABLE NAMES wayland-scanner REQUIRED)
# Prefer the system wayland-protocols XML tree. When it is older than wlroots
# needs (or absent), cmake/wlroots.cmake builds a vendored fallback — but that
# install tree does not exist at configure time, so read the in-tree source
# copy (which shares the stable/staging/unstable layout with the installed
# pkgdatadir) directly instead.
if(FLAKEWM_SYSTEM_WAYLAND_PROTOCOLS_FOUND AND
   NOT FLAKEWM_SYSTEM_WAYLAND_PROTOCOLS_VERSION VERSION_LESS "1.47")
  pkg_get_variable(WAYLAND_PROTOCOLS_DATA_DIR wayland-protocols pkgdatadir)
else()
  set(WAYLAND_PROTOCOLS_DATA_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/wayland-protocols"
  )
endif()

set(FLAKEWM_PROTOCOL_DIR "${CMAKE_CURRENT_BINARY_DIR}/protocol")
file(MAKE_DIRECTORY "${FLAKEWM_PROTOCOL_DIR}")

function(flakewm_generate_server_protocol target xml basename)
  set(header "${FLAKEWM_PROTOCOL_DIR}/${basename}-protocol.h")
  set(code "${FLAKEWM_PROTOCOL_DIR}/${basename}-protocol.c")
  add_custom_command(
    OUTPUT "${header}" "${code}"
    COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" server-header "${xml}" "${header}"
    COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" private-code "${xml}" "${code}"
    DEPENDS "${xml}"
    VERBATIM
  )
  set(${target} "${header}" "${code}" PARENT_SCOPE)
endfunction()

flakewm_generate_server_protocol(
  FLAKEWM_TEXT_INPUT_V1_PROTOCOL
  "${WAYLAND_PROTOCOLS_DATA_DIR}/unstable/text-input/text-input-unstable-v1.xml"
  text-input-unstable-v1
)
flakewm_generate_server_protocol(
  FLAKEWM_TEXT_INPUT_V2_PROTOCOL
  "${CMAKE_CURRENT_SOURCE_DIR}/libs/waylib/src/server/protocols/private/text-input-unstable-v2.xml"
  text-input-unstable-v2
)
flakewm_generate_server_protocol(
  FLAKEWM_INPUT_TIMESTAMPS_V1_PROTOCOL
  "${WAYLAND_PROTOCOLS_DATA_DIR}/unstable/input-timestamps/input-timestamps-unstable-v1.xml"
  input-timestamps-unstable-v1
)
flakewm_generate_server_protocol(
  FLAKEWM_XDG_TOPLEVEL_DRAG_V1_PROTOCOL
  "${WAYLAND_PROTOCOLS_DATA_DIR}/staging/xdg-toplevel-drag/xdg-toplevel-drag-v1.xml"
  xdg-toplevel-drag-v1
)

# KDE/Plasma compatibility protocols.  These XML files are kept in-tree so
# the compositor advertises exactly the interface revisions implemented below,
# independently of the plasma-wayland-protocols version installed by a distro.
foreach(protocol IN ITEMS
    blur
    dpms
    idle
    kde-keystate
    kde-output-device-v2
    kde-output-management-v2
    kde-primary-output-v1
    plasma-shell
    plasma-virtual-desktop
    plasma-window-management
  )
  string(REPLACE "-" "_" protocol_variable "${protocol}")
  string(TOUPPER "${protocol_variable}" protocol_variable)
  flakewm_generate_server_protocol(
    "FLAKEWM_${protocol_variable}_PROTOCOL"
    "${CMAKE_CURRENT_SOURCE_DIR}/protocols/${protocol}.xml"
    "${protocol}"
  )
  list(APPEND FLAKEWM_KDE_PROTOCOLS ${FLAKEWM_${protocol_variable}_PROTOCOL})
endforeach()

# GXDE-native protocols, kept at the exact revisions implemented by the
# server-side compatibility manager.
foreach(protocol IN ITEMS
    gxde-identifier-v1
    gxde-screenshot-v1
  )
  string(REPLACE "-" "_" protocol_variable "${protocol}")
  string(TOUPPER "${protocol_variable}" protocol_variable)
  flakewm_generate_server_protocol(
    "FLAKEWM_${protocol_variable}_PROTOCOL"
    "${CMAKE_CURRENT_SOURCE_DIR}/protocols/${protocol}.xml"
    "${protocol}"
  )
  list(APPEND FLAKEWM_GXDE_PROTOCOLS ${FLAKEWM_${protocol_variable}_PROTOCOL})
endforeach()

# Treeland and legacy DDE compatibility protocols used by GXDE/DTK.
foreach(protocol IN ITEMS
    dde-shell
    treeland-app-id-resolver-v1
    treeland-capture-unstable-v1
    treeland-dde-shell-v1
    treeland-personalization-manager-v1
  )
  string(REPLACE "-" "_" protocol_variable "${protocol}")
  string(TOUPPER "${protocol_variable}" protocol_variable)
  flakewm_generate_server_protocol(
    "FLAKEWM_${protocol_variable}_PROTOCOL"
    "${CMAKE_CURRENT_SOURCE_DIR}/protocols/${protocol}.xml"
    "${protocol}"
  )
  list(APPEND FLAKEWM_TREELAND_PROTOCOLS
    ${FLAKEWM_${protocol_variable}_PROTOCOL}
  )
endforeach()

# UKUI compatibility protocols, kept in-tree at the revisions implemented by
# the server-side compatibility manager.
foreach(protocol IN ITEMS
    ukui-blur-v1
    ukui-output-v1
    ukui-shell
    ukui-window-management
  )
  string(REPLACE "-" "_" protocol_variable "${protocol}")
  string(TOUPPER "${protocol_variable}" protocol_variable)
  flakewm_generate_server_protocol(
    "FLAKEWM_${protocol_variable}_PROTOCOL"
    "${CMAKE_CURRENT_SOURCE_DIR}/protocols/${protocol}.xml"
    "${protocol}"
  )
  list(APPEND FLAKEWM_UKUI_PROTOCOLS ${FLAKEWM_${protocol_variable}_PROTOCOL})
endforeach()

add_executable(flakewm-xwayland
  src/xwayland/server/server.cc
)

target_compile_definitions(flakewm-xwayland PRIVATE
  FLAKEWM_XWAYLAND_PATH="${XWAYLAND_EXECUTABLE}"
)

set(flakewm_CXX_SOURCES
  src/main.cc
  src/backend/backend/backend.cc
  src/backend/fbdev/fbdev_backend/fbdev_backend.cc
  src/backend/fbdev/fbdev_output/fbdev_output.cc
  src/core/compositor/compositor.cc
  src/core/compositor_private/compositor_private.cc
  src/dbus/wlcom/wlcom_dbus_desktop.cc
  src/dbus/wlcom/wlcom_dbus_input.cc
  src/dbus/wlcom/wlcom_dbus_kglobalaccel.cc
  src/dbus/wlcom/wlcom_dbus_manager.cc
  src/dbus/wlcom/wlcom_dbus_output.cc
  src/dbus/wlcom/wlcom_dbus_screenshot.cc
  src/dbus/wlcom/wlcom_dbus_view.cc
  src/input/key_binding_manager.cc
  src/input/touchpad_manager.cc
  src/render/backdrop_blur_renderer.cc
  src/render/vulkan_blur_pipeline.cc
  src/view/app_switcher/app_switcher.cc
  src/view/multitasking/multitasking.cc
  src/view/multitasking/multitasking_layout.cc
  src/view/multitasking/multitasking_renderer.cc
  src/view/multitasking/multitasking_wallpaper.cc
  src/view/touch_feedback.cc
  src/view/shake_cursor/shake_cursor.cc
  src/view/window_previews/window_previews.cc
  src/view/window_previews/window_previews_layout.cc
  src/view/window_previews/window_previews_renderer.cc
  src/view/window_selecter/window_selector.cc
  src/protocol/foreign_toplevel/foreign_toplevel.cc
  src/protocol/gxde/gxde_identifier.cc
  src/protocol/gxde/gxde_protocol_manager.cc
  src/protocol/gxde/gxde_screenshot.cc
  src/protocol/input_method/input_method_popup.cc
  src/protocol/input_method/input_method_relay.cc
  src/protocol/layer_shell/layer_surface.cc
  src/protocol/input_timestamps/input_timestamps_manager.cc
  src/protocol/kde/kde_idle_manager.cc
  src/protocol/kde/kde_output_manager.cc
  src/protocol/kde/kde_protocol_manager.cc
  src/protocol/protocol_manager/protocol_manager.cc
  src/protocol/text_input/text_input.cc
  src/protocol/text_input/text_input_v1.cc
  src/protocol/text_input/text_input_v1_manager.cc
  src/protocol/text_input/text_input_v2.cc
  src/protocol/text_input/text_input_v2_manager.cc
  src/protocol/text_input/text_input_v3.cc
  src/protocol/toplevel_drag/toplevel_drag.cc
  src/protocol/toplevel_drag/toplevel_drag_manager.cc
  src/protocol/treeland/dde_shell.cc
  src/protocol/treeland/treeland_app_id_resolver.cc
  src/protocol/treeland/treeland_capture.cc
  src/protocol/treeland/treeland_dde_shell.cc
  src/protocol/treeland/treeland_color_scheme.cc
  src/protocol/treeland/treeland_personalization.cc
  src/protocol/treeland/treeland_protocol_manager.cc
  src/protocol/ukui/ukui_blur.cc
  src/protocol/ukui/ukui_output.cc
  src/protocol/ukui/ukui_protocol_manager.cc
  src/protocol/ukui/ukui_shell.cc
  src/protocol/ukui/ukui_window_management.cc
  src/view/ssd/ssd/ssd.cc
  src/view/ssd/ssd_buffer/ssd_buffer.cc
  src/view/ssd/popup_renderer/popup_animation.cc
  src/view/ssd/popup_renderer/popup_renderer.cc
  src/view/ssd/popup_shadow/popup_shadow.cc
  src/view/ssd/ssd_renderer/ssd_renderer.cc
  src/view/ssd/ssd_shadow/ssd_shadow.cc
  src/view/ssd/ssd_surface_clip/ssd_surface_clip.cc
  src/view/ssd/split_screen_switcher/split_screen_switcher.cc
  src/view/ssd/split_screen_switcher/tile_animation.cc
  src/view/ssd/titlebar_tooltip/titlebar_tooltip.cc
  src/view/ssd/window_menu/window_menu.cc
  src/view/ssd/window_menu/window_menu_renderer.cc
  src/view/ssd/window_icon_provider/window_icon_provider.cc
  src/xwayland/xsurface/xsurface.cc
  src/xwayland/xwayland_manager/xwayland_manager.cc
  src/utils/args_handler/args_handler.cc
  src/utils/log/log.cc
  src/utils/misc/misc.cc
)

add_executable(flakewm
  ${flakewm_CXX_SOURCES}
  ${FLAKEWM_TEXT_INPUT_V1_PROTOCOL}
  ${FLAKEWM_TEXT_INPUT_V2_PROTOCOL}
  ${FLAKEWM_INPUT_TIMESTAMPS_V1_PROTOCOL}
  ${FLAKEWM_XDG_TOPLEVEL_DRAG_V1_PROTOCOL}
  ${FLAKEWM_GXDE_PROTOCOLS}
  ${FLAKEWM_KDE_PROTOCOLS}
  ${FLAKEWM_TREELAND_PROTOCOLS}
  ${FLAKEWM_UKUI_PROTOCOLS}
  src/wlr_wrapper/wlroots.h
  src/wlr_wrapper/color.h
  src/wlr_wrapper/scene.h
  src/wlr_wrapper/wlr_input_method.h
  src/wlr_wrapper/wlr_layer_shell.h
  src/wlr_wrapper/wlr_xwayland.h
  src/backend/backend/backend.h
  src/backend/fbdev/fbdev_backend/fbdev_backend.h
  src/backend/fbdev/fbdev_output/fbdev_output.h
  src/core/compositor/compositor.h
  src/core/compositor_private/compositor_private.h
  src/dbus/wlcom/wlcom_dbus_manager.h
  src/dbus/wlcom/wlcom_dbus_manager_p.h
  src/input/key_binding_manager.h
  src/input/touchpad_manager.h
  src/render/backdrop_blur_renderer.h
  src/render/vulkan_blur_pipeline.h
  src/view/app_switcher/app_switcher.h
  src/view/multitasking/multitasking.h
  src/view/multitasking/multitasking_renderer.h
  src/view/multitasking/multitasking_wallpaper.h
  src/view/touch_feedback.h
  src/view/shake_cursor/shake_cursor.h
  src/view/window_previews/window_previews.h
  src/view/window_previews/window_previews_renderer.h
  src/view/window_selecter/window_selector.h
  src/protocol/foreign_toplevel/foreign_toplevel.h
  src/protocol/gxde/gxde_protocol_manager.h
  src/protocol/gxde/gxde_protocol_manager_internal.h
  src/protocol/input_method/input_method_popup.h
  src/protocol/input_method/input_method_relay.h
  src/protocol/layer_shell/layer_surface.h
  src/protocol/input_timestamps/input_timestamps_manager.h
  src/protocol/kde/kde_idle_manager.h
  src/protocol/kde/kde_output_manager.h
  src/protocol/kde/kde_protocol_manager.h
  src/protocol/protocol_manager/protocol_manager.h
  src/protocol/text_input/text_input.h
  src/protocol/text_input/text_input_v1.h
  src/protocol/text_input/text_input_v1_manager.h
  src/protocol/text_input/text_input_v2.h
  src/protocol/text_input/text_input_v2_manager.h
  src/protocol/text_input/text_input_v3.h
  src/protocol/toplevel_drag/toplevel_drag.h
  src/protocol/toplevel_drag/toplevel_drag_manager.h
  src/protocol/treeland/treeland_protocol_manager.h
  src/protocol/treeland/treeland_protocol_manager_internal.h
  src/protocol/ukui/ukui_protocol_manager.h
  src/protocol/ukui/ukui_protocol_manager_internal.h
  src/view/ssd/ssd/ssd.h
  src/view/ssd/ssd_buffer/ssd_buffer.h
  src/view/ssd/popup_renderer/popup_animation.h
  src/view/ssd/popup_renderer/popup_renderer.h
  src/view/ssd/popup_shadow/popup_shadow.h
  src/view/ssd/ssd_renderer/ssd_renderer.h
  src/view/ssd/ssd_shadow/ssd_shadow.h
  src/view/ssd/ssd_surface_clip/ssd_surface_clip.h
  src/view/ssd/split_screen_switcher/split_screen_switcher.h
  src/view/ssd/split_screen_switcher/tile_animation.h
  src/view/ssd/titlebar_tooltip/titlebar_tooltip.h
  src/view/ssd/window_menu/window_menu.h
  src/view/ssd/window_menu/window_menu_renderer.h
  src/view/ssd/window_icon_provider/window_icon_provider.h
  src/xwayland/xsurface/xsurface.h
  src/xwayland/xwayland_manager/xwayland_manager.h
  src/utils/args_handler/args_handler.h
  src/utils/log/log.h
  src/utils/misc/misc.h
  src/utils/signal_listener.h
)

qt_add_resources(flakewm "ssd_qml"
  PREFIX "/flakewm"
  BASE src/view/ssd
  FILES
    src/view/ssd/ssd.qml
    src/view/ssd/icons/close_hover.svg
    src/view/ssd/icons/close_normal.svg
    src/view/ssd/icons/close_press.svg
    src/view/ssd/icons/maximize_hover.svg
    src/view/ssd/icons/maximize_normal.svg
    src/view/ssd/icons/maximize_press.svg
    src/view/ssd/icons/minimize_hover.svg
    src/view/ssd/icons/minimize_normal.svg
    src/view/ssd/icons/minimize_press.svg
    src/view/ssd/icons/unmaximize_hover.svg
    src/view/ssd/icons/unmaximize_normal.svg
    src/view/ssd/icons/unmaximize_press.svg
)

qt_add_resources(flakewm "window_menu_qml"
  PREFIX "/flakewm"
  BASE src/view/ssd/window_menu
  FILES src/view/ssd/window_menu/window_menu.qml
)

qt_add_resources(flakewm "titlebar_hover_qml"
  PREFIX "/flakewm"
  BASE src/view/ssd
  FILES
    src/view/ssd/split_screen_switcher/split_screen_switcher.qml
    src/view/ssd/titlebar_tooltip/titlebar_tooltip.qml
)

qt_add_resources(flakewm "touch_feedback_qml"
  PREFIX "/flakewm"
  BASE src/view
  FILES src/view/touch_feedback.qml
)

qt_add_resources(flakewm "app_switcher_qml"
  PREFIX "/flakewm"
  BASE src/view/app_switcher
  FILES
    src/view/app_switcher/app_switcher.qml
    src/view/app_switcher/kwintabboxsettings.kcfg
)

qt_add_resources(flakewm "multitasking_qml"
  PREFIX "/flakewm"
  BASE src/view/multitasking
  FILES
    src/view/multitasking/multitasking.qml
    src/view/multitasking/icons/add-light.svg
    src/view/multitasking/icons/multiview_delete.svg
    src/view/multitasking/icons/multiview_top.svg
    src/view/multitasking/icons/multiview_top_active.svg
)

qt_add_resources(flakewm "window_previews_qml"
  PREFIX "/flakewm"
  BASE src/view/window_previews
  FILES src/view/window_previews/window_previews.qml
)

qt_add_resources(flakewm "window_selector_qml"
  PREFIX "/flakewm"
  BASE src/view/window_selecter
  FILES
    src/view/window_selecter/window_selector_toolbar.qml
    src/view/window_selecter/icons/select_output.svg
    src/view/window_selecter/icons/select_region.svg
    src/view/window_selecter/icons/select_window.svg
)

add_dependencies(flakewm flakewm-xwayland)

target_link_libraries(flakewm
  PRIVATE
    Wlroots::wlroots
    Qt6::DBus
    Qt6::Qml
    Qt6::Quick
    absl::log
    absl::log_initialize
)

target_include_directories(flakewm PRIVATE ./ "${CMAKE_CURRENT_BINARY_DIR}")

target_compile_definitions(flakewm PRIVATE
  FLAKEWM_VERSION="${PROJECT_VERSION}"
)

if(QGSETTINGS_QT6_INCLUDE_DIR AND QGSETTINGS_QT6_LIBRARY)
  target_include_directories(flakewm PRIVATE "${QGSETTINGS_QT6_INCLUDE_DIR}")
  target_link_libraries(flakewm PRIVATE "${QGSETTINGS_QT6_LIBRARY}")
  target_compile_definitions(flakewm PRIVATE FLAKEWM_HAS_QGSETTINGS=1)
else()
  message(WARNING
    "FLAKEWM: Qt6 QGSettings not found; touchpads will use safe defaults"
  )
endif()

find_program(CPPLINT_EXECUTABLE NAMES cpplint)
if(CPPLINT_EXECUTABLE)
  add_custom_target(cpplint
    COMMAND ${CPPLINT_EXECUTABLE} --recursive src
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMENT "Checking Google C++ style with cpplint"
    VERBATIM
  )
endif()

find_program(CLANG_TIDY_EXECUTABLE NAMES clang-tidy)
if(CLANG_TIDY_EXECUTABLE)
  add_custom_target(clang-tidy
    COMMAND ${CLANG_TIDY_EXECUTABLE}
      -p=${CMAKE_BINARY_DIR}
      ${flakewm_CXX_SOURCES}
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMENT "Running clang-tidy"
    VERBATIM
  )
endif()

include(GNUInstallDirs)

install(TARGETS flakewm flakewm-xwayland
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
