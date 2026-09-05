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

find_program(XWAYLAND_EXECUTABLE NAMES Xwayland REQUIRED)

add_executable(flakewm-xwayland
  src/xwayland/server.cc
)

target_compile_definitions(flakewm-xwayland PRIVATE
  FLAKEWM_XWAYLAND_PATH="${XWAYLAND_EXECUTABLE}"
)

set(flakewm_CXX_SOURCES
  src/main.cc
  src/core/compositor/compositor.cc
  src/core/compositor_private/compositor_private.cc
  src/protocol/layer_shell/layer_surface.cc
  src/xwayland/xsurface.cc
  src/xwayland/xwayland.cc
  src/utils/args_handler/args_handler.cc
  src/utils/log/log.cc
  src/utils/misc/misc.cc
)

add_executable(flakewm
  ${flakewm_CXX_SOURCES}
  src/wlroots.h
  src/core/compositor/compositor.h
  src/core/compositor_private/compositor_private.h
  src/protocol/layer_shell/layer_surface.h
  src/protocol/layer_shell/wlr_layer_shell.h
  src/xwayland/wlr_xwayland.h
  src/xwayland/xsurface.h
  src/xwayland/xwayland.h
  src/utils/args_handler/args_handler.h
  src/utils/log/log.h
  src/utils/misc/misc.h
  src/utils/signal_listener.h
)

add_dependencies(flakewm flakewm-xwayland)

target_link_libraries(flakewm
  PRIVATE
    Wlroots::wlroots
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
