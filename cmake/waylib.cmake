# Copyright (C) 2026 CharOfString <root@charofstring.cc>
#
# This file is part of FLAKEWM.
#
# FLAKEWM is free software: you can redistribute it and/or modify it under the
# terms of the GNU General Public License as published by the Free Software
# Foundation, either version 3 of the License, or (at your option) any later
# version.
#
# FLAKEWM is distributed in the hope that it will be useful, but WITHOUT ANY
# WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
# details.
#
# You should have received a copy of the GNU General Public License along with
# FLAKEWM. If not, see <https://www.gnu.org/licenses/>.
# -----------------------------------------------------------------------------
# This file is in charge of selecting and configuring Waylib.

include_guard(GLOBAL)

# We allow you to use your system Waylib package by manually enabling
# FLAKEWM_USE_SYSTEM_WAYLIB, but it is your responsibility to ensure that
# your system Waylib package is compatible with FLAKEWM.
option(FLAKEWM_USE_SYSTEM_WAYLIB
  "Use a system-installed Waylib package"
  OFF
)

if(FLAKEWM_USE_SYSTEM_WAYLIB)
  find_package(Waylib REQUIRED COMPONENTS Server)
  return()
endif()

# The paths are calculated from the current cmake/ directory so this file can
# also be included by a parent project.
get_filename_component(FLAKEWM_ROOT_DIR
  "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE
)

set(FLAKEWM_VENDOR_WAYLIB_SOURCE_DIR
  "${FLAKEWM_ROOT_DIR}/libs/waylib"
)
set(FLAKEWM_VENDOR_WLROOTS_SOURCE_DIR
  "${FLAKEWM_ROOT_DIR}/libs/wlroots"
)

foreach(required_file
  "${FLAKEWM_VENDOR_WAYLIB_SOURCE_DIR}/CMakeLists.txt"
  "${FLAKEWM_VENDOR_WLROOTS_SOURCE_DIR}/meson.build"
)
  if(NOT EXISTS "${required_file}")
    message(FATAL_ERROR
      "(Setup) Configuration: Halted due to ${required_file} is missing."
    )
  endif()
endforeach()

# wlroots.cmake provides an ExternalProject and a build-time pkg-config file.
# Not defining Wlroots::wlroots here makes the unmodified Waylib use its
# supported standalone pkg-config path.
include("${CMAKE_CURRENT_LIST_DIR}/wlroots.cmake")

set(BUILD_EXAMPLES OFF CACHE BOOL "Build Waylib examples" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "Build Waylib tests" FORCE)
set(TREELAND_INSTALL_DEV OFF CACHE BOOL
  "Install Waylib development files" FORCE
)

# Prefer the project-provided protocol package for the protocol required by
# this Waylib snapshot.
set(TreelandProtocols_DIR
  "${CMAKE_CURRENT_LIST_DIR}/TreelandProtocols"
  CACHE PATH "TreelandProtocols package used by vendored Waylib" FORCE
)

set(_FLAKEWM_saved_pkg_config_path "$ENV{PKG_CONFIG_PATH}")
if(_FLAKEWM_saved_pkg_config_path)
  set(ENV{PKG_CONFIG_PATH}
    "${WLROOTS_PKGCONFIG_DIR}:${_FLAKEWM_saved_pkg_config_path}"
  )
else()
  set(ENV{PKG_CONFIG_PATH} "${WLROOTS_PKGCONFIG_DIR}")
endif()

set(FLAKEWM_VENDOR_WAYLIB_BINARY_DIR
  "${CMAKE_BINARY_DIR}/_deps/waylib"
)
add_subdirectory(
  "${FLAKEWM_VENDOR_WAYLIB_SOURCE_DIR}"
  "${FLAKEWM_VENDOR_WAYLIB_BINARY_DIR}"
)

set(ENV{PKG_CONFIG_PATH} "${_FLAKEWM_saved_pkg_config_path}")

# Preserve the future wlroots archive directory and, when a compatibility
# fallback is selected, its configure-time linker-script directory.
target_link_directories(waylibserver PUBLIC
  ${WLROOTS_LINK_DIRECTORIES}
)
set_property(TARGET waylibserver APPEND PROPERTY
  BUILD_RPATH "${WLROOTS_RUNTIME_LIBRARY_DIRS}"
)
if(WLROOTS_RUNTIME_LIBRARY_DIRS)
  list(JOIN WLROOTS_RUNTIME_LIBRARY_DIRS ":" _FLAKEWM_vendor_build_rpath)
  target_link_options(waylibserver INTERFACE
    "LINKER:-rpath,${_FLAKEWM_vendor_build_rpath}"
  )
endif()

add_dependencies(waylibserver wlroots_external)
