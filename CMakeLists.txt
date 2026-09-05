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

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick)
find_program(XWAYLAND_EXECUTABLE NAMES Xwayland REQUIRED)

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
  src/protocol/layer_shell/layer_surface.cc
  src/view/ssd/ssd/ssd.cc
  src/view/ssd/ssd_buffer/ssd_buffer.cc
  src/view/ssd/ssd_renderer/ssd_renderer.cc
  src/view/ssd/ssd_shadow/ssd_shadow.cc
  src/view/ssd/ssd_surface_clip/ssd_surface_clip.cc
  src/view/ssd/window_icon_provider/window_icon_provider.cc
  src/xwayland/xsurface/xsurface.cc
  src/xwayland/xwayland_manager/xwayland_manager.cc
  src/utils/args_handler/args_handler.cc
  src/utils/log/log.cc
  src/utils/misc/misc.cc
)

add_executable(flakewm
  ${flakewm_CXX_SOURCES}
  src/wlr_wrapper/wlroots.h
  src/wlr_wrapper/color.h
  src/wlr_wrapper/scene.h
  src/wlr_wrapper/wlr_layer_shell.h
  src/wlr_wrapper/wlr_xwayland.h
  src/backend/backend/backend.h
  src/backend/fbdev/fbdev_backend/fbdev_backend.h
  src/backend/fbdev/fbdev_output/fbdev_output.h
  src/core/compositor/compositor.h
  src/core/compositor_private/compositor_private.h
  src/protocol/layer_shell/layer_surface.h
  src/view/ssd/ssd/ssd.h
  src/view/ssd/ssd_buffer/ssd_buffer.h
  src/view/ssd/ssd_renderer/ssd_renderer.h
  src/view/ssd/ssd_shadow/ssd_shadow.h
  src/view/ssd/ssd_surface_clip/ssd_surface_clip.h
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

add_dependencies(flakewm flakewm-xwayland)

target_link_libraries(flakewm
  PRIVATE
    Wlroots::wlroots
    Qt6::Qml
    Qt6::Quick
    absl::log
    absl::log_initialize
)

target_include_directories(flakewm PRIVATE ./)

target_compile_definitions(flakewm PRIVATE
  FLAKEWM_VERSION="${PROJECT_VERSION}"
)

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
