# Copyright (C) 2026 CharOfString <root@charofstring.cc>
#
# This file is part of FlakeWM.
#
# FlakeWM is free software: you can redistribute it and/or modify it under the
# terms of the GNU General Public License as published by the Free Software
# Foundation, either version 3 of the License, or (at your option) any later
# version.
#
# FlakeWM is distributed in the hope that it will be useful, but WITHOUT ANY
# WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along with
# FlakeWM. If not, see <https://www.gnu.org/licenses/>.
# -----------------------------------------------------------------------------
# This file is in charge of handling paths of vendored Waylib.

# Wlroots is built via Meson, so Meson is actually REQUIRED.
find_program(FLAKEWM_MESON_EXECUTABLE NAMES meson REQUIRED)

# The paths is actually calculated from the current cmake/ directory.
get_filename_component(FLAKEWM_ROOT_DIR
  "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE
)

# Define the source directory of vendored Waylib.
set(FLAKEWM_VENDOR_WAYLIB_SOURCE_DIR "${FLAKEWM_ROOT_DIR}/vendor/waylib")

# Define the source directory of vendored QWlroots (which is brought by Waylib).
set(FLAKEWM_VENDOR_QWLROOTS_SOURCE_DIR
  "${FLAKEWM_VENDOR_WAYLIB_SOURCE_DIR}/qwlroots"
)

# Define the source directory of vendored Wlroots (which is brought by QWlroots).
set(FLAKEWM_VENDOR_WLROOTS_SOURCE_DIR
  "${FLAKEWM_VENDOR_QWLROOTS_SOURCE_DIR}/wlroots"
)

# Do a quick check if the build files are present.
foreach(required_file
  "${FLAKEWM_VENDOR_WAYLIB_SOURCE_DIR}/CMakeLists.txt"
  "${FLAKEWM_VENDOR_QWLROOTS_SOURCE_DIR}/CMakeLists.txt"
  "${FLAKEWM_VENDOR_WLROOTS_SOURCE_DIR}/meson.build"
)
  if(NOT EXISTS "${required_file}")
    message(FATAL_ERROR
      "(Setup) Configuration: Halted due to ${required_file} is missing."
    )
  endif()
endforeach()

# The artifacts of dependencies is going to _deps/ under the CMake build directory.
set(FLAKEWM_VENDOR_WLROOTS_BINARY_DIR
  "${CMAKE_BINARY_DIR}/_deps/wlroots-build"
)

# The .pc file of Wlroots is going to _deps/ under the CMake build directory.
set(FLAKEWM_VENDOR_WLROOTS_PKGCONFIG_DIR
  "${CMAKE_BINARY_DIR}/_deps/wlroots-pkgconfig"
)

# Map CMake build type to Meson's.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(_flakewm_wlroots_buildtype debug)
elseif(CMAKE_BUILD_TYPE STREQUAL "Release" OR
    CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
  set(_flakewm_wlroots_buildtype release)
else()
  set(_flakewm_wlroots_buildtype debugoptimized)
endif()

# Compose the argument for meson setup.
# If build.ninja is present, it means that meson has been configured and we use
# --reconfigure to update that.
# Otherwise we do a fresh config, building Wlroots as a shared library and skip
# building examples.
set(_flakewm_meson_setup_args setup)

if(EXISTS "${FLAKEWM_VENDOR_WLROOTS_BINARY_DIR}/build.ninja")
  list(APPEND _flakewm_meson_setup_args --reconfigure)
endif()
list(APPEND _flakewm_meson_setup_args
  "${FLAKEWM_VENDOR_WLROOTS_BINARY_DIR}"
  "${FLAKEWM_VENDOR_WLROOTS_SOURCE_DIR}"
  "--buildtype=${_flakewm_wlroots_buildtype}"
  --default-library=shared
  -Dexamples=false
)

# When configuring, ensure that CMake and Meson setup use the same C/C++
# compiler.
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    "CC=${CMAKE_C_COMPILER}"
    "${FLAKEWM_MESON_EXECUTABLE}" ${_flakewm_meson_setup_args}
  COMMAND_ERROR_IS_FATAL ANY
)

# Meson's uninstalled .pc file has correct build tree paths, but lacks the
# feature variables used by QWlroots. Therefore, we read the install .pc file
# that contains these variables, and rewrite the paths to the current build
# directory instead of the install directory.
set(_flakewm_wlroots_pc_source
  "${FLAKEWM_VENDOR_WLROOTS_BINARY_DIR}/meson-private/wlroots-0.19.pc"
)
if(NOT EXISTS "${_flakewm_wlroots_pc_source}")
  message(FATAL_ERROR
    "(Setup) Wlroots: Halted due to Meson does NOT generated the pc file."
  )
endif()

# Fix prefix, generated headers directory, library directory and compile flags.
# Cflags must include both the generated headers in the Meson build directory
# and the public headers in the wlroots source directory.
file(READ "${_flakewm_wlroots_pc_source}" _flakewm_wlroots_pc)
string(REGEX REPLACE "prefix=[^\n]*"
  "prefix=${FLAKEWM_VENDOR_WLROOTS_BINARY_DIR}"
  _flakewm_wlroots_pc "${_flakewm_wlroots_pc}"
)

string(REGEX REPLACE "includedir=[^\n]*"
  "includedir=${FLAKEWM_VENDOR_WLROOTS_BINARY_DIR}/include"
  _flakewm_wlroots_pc "${_flakewm_wlroots_pc}"
)

string(REGEX REPLACE "libdir=[^\n]*"
  "libdir=${FLAKEWM_VENDOR_WLROOTS_BINARY_DIR}"
  _flakewm_wlroots_pc "${_flakewm_wlroots_pc}"
)

string(REGEX REPLACE "Cflags:[^\n]*"
  "Cflags: -I${FLAKEWM_VENDOR_WLROOTS_BINARY_DIR}/include -I${FLAKEWM_VENDOR_WLROOTS_BINARY_DIR}/protocol -I${FLAKEWM_VENDOR_WLROOTS_SOURCE_DIR}/include"
  _flakewm_wlroots_pc "${_flakewm_wlroots_pc}"
)

file(MAKE_DIRECTORY "${FLAKEWM_VENDOR_WLROOTS_PKGCONFIG_DIR}")
file(WRITE
  "${FLAKEWM_VENDOR_WLROOTS_PKGCONFIG_DIR}/wlroots-0.19.pc"
  "${_flakewm_wlroots_pc}"
)

# QWlroots and Waaylib will look for Wayland protocol XML files via the
# common wlr-protocols pkg-config interface.
# We already have vendored wlroots, so we have everything needed... I mean,
# EVERYTHING!!!
# The rest to do is simple, we copy them in standard
# share/wlr-protocols/unstable layout so that wlr-protocols package is no
# longer required to be installed.
set(_flakewm_wlr_protocols_dir
  "${CMAKE_BINARY_DIR}/_deps/wlr-protocols/share/wlr-protocols"
)
file(MAKE_DIRECTORY "${_flakewm_wlr_protocols_dir}/unstable")
foreach(protocol
  wlr-layer-shell-unstable-v1.xml
  wlr-output-power-management-unstable-v1.xml
)
  configure_file(
    "${FLAKEWM_VENDOR_WLROOTS_SOURCE_DIR}/protocol/${protocol}"
    "${_flakewm_wlr_protocols_dir}/unstable/${protocol}"
    COPYONLY
  )
endforeach()

# That .pc file is for build only, and we need to tell the location of
# protocol XML files via pkgdatadir.
file(WRITE "${FLAKEWM_VENDOR_WLROOTS_PKGCONFIG_DIR}/wlr-protocols.pc"
"prefix=${CMAKE_BINARY_DIR}/_deps/wlr-protocols
pkgdatadir=\${prefix}/share/wlr-protocols

Name: wlr-protocols
Description: Protocol files provided by vendored wlroots
Version: 0.19.0
")

# After meson setup, the actual wlroots build is wrapped into a CMake custom
# target so that we can add_dependencies to other projects later.
add_custom_target(flakewm-vendor-wlroots
  COMMAND "${FLAKEWM_MESON_EXECUTABLE}" compile
    -C "${FLAKEWM_VENDOR_WLROOTS_BINARY_DIR}"
  COMMENT "Building vendored wlroots"
  USES_TERMINAL
)

# Ask Waylib to use its submodule QWlroots and vendored wlroots 0.19.
# We also shuts down examples and UTs.
set(WITH_SUBMODULE_QWLROOTS ON CACHE BOOL
  "Use the vendored QWlroots from Waylib" FORCE
)
set(USE_WLROOTS_19 ON CACHE BOOL "Use the vendored wlroots 0.19" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "Build Waylib examples")
set(BUILD_TESTS OFF CACHE BOOL "Build Waylib tests")

# When configuring Waylib/QWlroots subdirectories, we temporarily prepend
# the vendored wlroots pkg-config directory to the search path.
# This ensures that we are using the vendored Wlroots, not system installed
# ones.
# WE WILL RESTORE THEM LATER.
set(_flakewm_saved_pkg_config_path "$ENV{PKG_CONFIG_PATH}")
if(_flakewm_saved_pkg_config_path)
  set(ENV{PKG_CONFIG_PATH}
    "${FLAKEWM_VENDOR_WLROOTS_PKGCONFIG_DIR}:${_flakewm_saved_pkg_config_path}"
  )
else()
  set(ENV{PKG_CONFIG_PATH} "${FLAKEWM_VENDOR_WLROOTS_PKGCONFIG_DIR}")
endif()

# Join Waylib to current project and put its artifcats to _deps/waylib.
set(FLAKEWM_VENDOR_WAYLIB_BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/waylib")
add_subdirectory(
  "${FLAKEWM_VENDOR_WAYLIB_SOURCE_DIR}"
  "${FLAKEWM_VENDOR_WAYLIB_BINARY_DIR}"
)

# Waylib/QWlroots has been configured, restore the original pkg-config search
# path to avoid affecting other dependencies configured later.
set(ENV{PKG_CONFIG_PATH} "${_flakewm_saved_pkg_config_path}")

# Install the vendored Wlroots in a FlakeWM-private directory. Installing it
# directly into CMAKE_INSTALL_LIBDIR could overwrite a system Wlroots with the
# same SONAME (libwlroots-0.19.so).
set(FLAKEWM_VENDOR_WLROOTS_INSTALL_DIR
  "${CMAKE_INSTALL_LIBDIR}/flakewm"
)
install(FILES
  "${FLAKEWM_VENDOR_WLROOTS_BINARY_DIR}/libwlroots-0.19.so"
  DESTINATION "${FLAKEWM_VENDOR_WLROOTS_INSTALL_DIR}"
  COMPONENT Runtime
)

# QWlroots and Waylib server will use Wlroots generated headers and shared
# library. Add the Meson build directory to their build-only link search path,
# then make them wait for flakewm-vendor-wlroots to finish. At install time,
# make each direct consumer load the private copy next to its normal library
# directory instead of a system Wlroots with the same SONAME.
# TARGET check allows some optional targets not created by the current
# Waylib configuration.
foreach(vendor_target qwlroots waylibserver)
  if(TARGET "${vendor_target}")
    target_link_directories("${vendor_target}" BEFORE PUBLIC
      "$<BUILD_INTERFACE:${FLAKEWM_VENDOR_WLROOTS_BINARY_DIR}>"
    )
    add_dependencies("${vendor_target}" flakewm-vendor-wlroots)
    set_property(TARGET "${vendor_target}" APPEND PROPERTY
      INSTALL_RPATH "$ORIGIN/flakewm"
    )
  endif()
endforeach()
