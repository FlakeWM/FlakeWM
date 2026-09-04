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
# -----------------------------------------------------------------------------
# This file handles Wlroots dependency management. Wlroots is built using Meson
# so this adapter is needed when using CMake.

include_guard(GLOBAL)
include(ExternalProject)

if(NOT DEFINED FLAKEWM_ROOT_DIR)
  get_filename_component(FLAKEWM_ROOT_DIR
    "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE
  )
endif()

find_program(MESON_EXECUTABLE meson REQUIRED)
find_package(PkgConfig REQUIRED)

option(FLAKEWM_ALLOW_VENDORED_WLROOTS_DEPS
  "Allow vendored fallbacks when system wlroots dependencies are too old"
  ON
)

# Keep foundational libraries supplied by the operating system whenever they
# satisfy wlroots. Vendored copies are compatibility fallbacks for old systems,
# not the normal dependency strategy.
pkg_check_modules(FLAKEWM_SYSTEM_WAYLAND_SERVER QUIET wayland-server)
pkg_check_modules(FLAKEWM_SYSTEM_WAYLAND_CLIENT QUIET wayland-client)
pkg_check_modules(FLAKEWM_SYSTEM_WAYLAND_SCANNER QUIET wayland-scanner)
pkg_check_modules(FLAKEWM_SYSTEM_LIBDRM QUIET libdrm)
pkg_check_modules(FLAKEWM_SYSTEM_PIXMAN QUIET pixman-1)

# Everything not vendored by this project remains a hard system dependency.
pkg_check_modules(WLROOTS_REQUIRED_SYSTEM_DEPS REQUIRED
  xkbcommon>=1.8.0
  wayland-protocols>=1.47
  egl
  gbm>=21.1
  glesv2
  lcms2
  libudev
  libseat>=0.2.0
  libdisplay-info>=0.2.0
  libinput>=1.19.0
  xcb
  xcb-composite
  xcb-dri3
  xcb-errors
  xcb-ewmh
  xcb-icccm
  xcb-present
  xcb-render
  xcb-renderutil
  xcb-res
  xcb-shm
  xcb-xfixes>=1.15
  xcb-xinput
)

set(WLROOTS_VENDOR_PREFIX "${CMAKE_BINARY_DIR}/_deps/wlroots-dependencies")
set(WLROOTS_VENDOR_DEPENDENCY_TARGETS "")
set(WLROOTS_VENDOR_PKGCONFIG_DIRS "")
set(WLROOTS_LINK_DIRECTORIES "")
set(WLROOTS_RUNTIME_LIBRARY_DIRS "")

function(_flakewm_vendor_fallback_warning dependency detected required source)
  if(NOT FLAKEWM_ALLOW_VENDORED_WLROOTS_DEPS)
    message(FATAL_ERROR
      "FLAKEWM: system ${dependency} is ${detected}; wlroots requires "
      "${required}. Vendored fallback is disabled by "
      "FLAKEWM_ALLOW_VENDORED_WLROOTS_DEPS=OFF."
    )
  endif()

  message(WARNING
    "FLAKEWM: system ${dependency} is ${detected}; wlroots requires "
    "${required}. Falling back to ${source}. Vendoring foundational system "
    "libraries is a last-resort compatibility measure. FLAKEWM will use these "
    "copies only because the system dependency is too old."
  )
endfunction()

set(_flakewm_system_wayland_ok TRUE)
foreach(_component SERVER CLIENT SCANNER)
  if(NOT FLAKEWM_SYSTEM_WAYLAND_${_component}_FOUND OR
     FLAKEWM_SYSTEM_WAYLAND_${_component}_VERSION VERSION_LESS "1.24.0")
    set(_flakewm_system_wayland_ok FALSE)
  endif()
endforeach()

if(_flakewm_system_wayland_ok)
  message(STATUS
    "FLAKEWM: using system Wayland ${FLAKEWM_SYSTEM_WAYLAND_SERVER_VERSION}"
  )
else()
  string(CONCAT _flakewm_wayland_detected
    "server ${FLAKEWM_SYSTEM_WAYLAND_SERVER_VERSION}, "
    "client ${FLAKEWM_SYSTEM_WAYLAND_CLIENT_VERSION}, "
    "scanner ${FLAKEWM_SYSTEM_WAYLAND_SCANNER_VERSION}"
  )
  _flakewm_vendor_fallback_warning(
    "Wayland"
    "${_flakewm_wayland_detected}"
    ">= 1.24.0"
    "${FLAKEWM_ROOT_DIR}/libs/wayland"
  )

  set(WLROOTS_VENDOR_WAYLAND_PREFIX "${WLROOTS_VENDOR_PREFIX}/wayland")
  ExternalProject_Add(wlroots_vendor_wayland
    SOURCE_DIR "${FLAKEWM_ROOT_DIR}/libs/wayland"
    BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/wayland-build"
    CONFIGURE_COMMAND
      "${MESON_EXECUTABLE}" setup
      "<BINARY_DIR>"
      "<SOURCE_DIR>"
      "--prefix=${WLROOTS_VENDOR_WAYLAND_PREFIX}"
      "--libdir=lib"
      "--buildtype=debugoptimized"
      "--wrap-mode=nodownload"
      "-Dtests=false"
      "-Ddocumentation=false"
      "-Ddtd_validation=false"
    BUILD_COMMAND "${MESON_EXECUTABLE}" compile -C "<BINARY_DIR>"
    INSTALL_COMMAND "${MESON_EXECUTABLE}" install -C "<BINARY_DIR>"
    BUILD_ALWAYS TRUE
    BUILD_BYPRODUCTS
      "${WLROOTS_VENDOR_WAYLAND_PREFIX}/lib/libwayland-server.so"
      "${WLROOTS_VENDOR_WAYLAND_PREFIX}/lib/libwayland-client.so"
  )
  list(APPEND WLROOTS_VENDOR_DEPENDENCY_TARGETS wlroots_vendor_wayland)
  list(APPEND WLROOTS_VENDOR_PKGCONFIG_DIRS
    "${WLROOTS_VENDOR_WAYLAND_PREFIX}/lib/pkgconfig"
  )
  list(APPEND WLROOTS_RUNTIME_LIBRARY_DIRS
    "${WLROOTS_VENDOR_WAYLAND_PREFIX}/lib"
  )
endif()

if(FLAKEWM_SYSTEM_LIBDRM_FOUND AND
   NOT FLAKEWM_SYSTEM_LIBDRM_VERSION VERSION_LESS "2.4.129")
  message(STATUS
    "FLAKEWM: using system libdrm ${FLAKEWM_SYSTEM_LIBDRM_VERSION}"
  )
else()
  _flakewm_vendor_fallback_warning(
    "libdrm"
    "${FLAKEWM_SYSTEM_LIBDRM_VERSION}"
    ">= 2.4.129"
    "${FLAKEWM_ROOT_DIR}/libs/libdrm"
  )

  set(WLROOTS_VENDOR_LIBDRM_PREFIX "${WLROOTS_VENDOR_PREFIX}/libdrm")
  ExternalProject_Add(wlroots_vendor_libdrm
    SOURCE_DIR "${FLAKEWM_ROOT_DIR}/libs/libdrm"
    BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/libdrm-build"
    CONFIGURE_COMMAND
      "${MESON_EXECUTABLE}" setup
      "<BINARY_DIR>"
      "<SOURCE_DIR>"
      "--prefix=${WLROOTS_VENDOR_LIBDRM_PREFIX}"
      "--libdir=lib"
      "--buildtype=debugoptimized"
      "--wrap-mode=nodownload"
      "-Dauto_features=disabled"
      "-Dtests=false"
      "-Dinstall-test-programs=false"
    BUILD_COMMAND "${MESON_EXECUTABLE}" compile -C "<BINARY_DIR>"
    INSTALL_COMMAND "${MESON_EXECUTABLE}" install -C "<BINARY_DIR>"
    BUILD_ALWAYS TRUE
    BUILD_BYPRODUCTS
      "${WLROOTS_VENDOR_LIBDRM_PREFIX}/lib/libdrm.so"
  )
  list(APPEND WLROOTS_VENDOR_DEPENDENCY_TARGETS wlroots_vendor_libdrm)
  list(APPEND WLROOTS_VENDOR_PKGCONFIG_DIRS
    "${WLROOTS_VENDOR_LIBDRM_PREFIX}/lib/pkgconfig"
  )
  list(APPEND WLROOTS_RUNTIME_LIBRARY_DIRS
    "${WLROOTS_VENDOR_LIBDRM_PREFIX}/lib"
  )
endif()

if(FLAKEWM_SYSTEM_PIXMAN_FOUND AND
   NOT FLAKEWM_SYSTEM_PIXMAN_VERSION VERSION_LESS "0.46.0")
  message(STATUS
    "FLAKEWM: using system pixman ${FLAKEWM_SYSTEM_PIXMAN_VERSION}"
  )
else()
  _flakewm_vendor_fallback_warning(
    "pixman"
    "${FLAKEWM_SYSTEM_PIXMAN_VERSION}"
    ">= 0.46.0"
    "${FLAKEWM_ROOT_DIR}/libs/pixman"
  )

  set(WLROOTS_VENDOR_PIXMAN_PREFIX "${WLROOTS_VENDOR_PREFIX}/pixman")
  ExternalProject_Add(wlroots_vendor_pixman
    SOURCE_DIR "${FLAKEWM_ROOT_DIR}/libs/pixman"
    BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/pixman-build"
    CONFIGURE_COMMAND
      "${MESON_EXECUTABLE}" setup
      "<BINARY_DIR>"
      "<SOURCE_DIR>"
      "--prefix=${WLROOTS_VENDOR_PIXMAN_PREFIX}"
      "--libdir=lib"
      "--buildtype=debugoptimized"
      "--wrap-mode=nodownload"
      "-Dtests=disabled"
      "-Ddemos=disabled"
      "-Dgtk=disabled"
      "-Dlibpng=disabled"
      "-Dopenmp=disabled"
    BUILD_COMMAND "${MESON_EXECUTABLE}" compile -C "<BINARY_DIR>"
    INSTALL_COMMAND "${MESON_EXECUTABLE}" install -C "<BINARY_DIR>"
    BUILD_ALWAYS TRUE
    BUILD_BYPRODUCTS
      "${WLROOTS_VENDOR_PIXMAN_PREFIX}/lib/libpixman-1.so"
  )
  list(APPEND WLROOTS_VENDOR_DEPENDENCY_TARGETS wlroots_vendor_pixman)
  list(APPEND WLROOTS_VENDOR_PKGCONFIG_DIRS
    "${WLROOTS_VENDOR_PIXMAN_PREFIX}/lib/pkgconfig"
  )
  list(APPEND WLROOTS_RUNTIME_LIBRARY_DIRS
    "${WLROOTS_VENDOR_PIXMAN_PREFIX}/lib"
  )
endif()

set(WLROOTS_VERSION 0.20.2)
set(WLROOTS_VERSION_MAJOR 0)
set(WLROOTS_VERSION_MINOR 20)
set(WLROOTS_VERSION_PATCH 2)

set(WLROOTS_SOURCE_DIR "${FLAKEWM_ROOT_DIR}/libs/wlroots")
set(WLROOTS_BUILD_DIR "${CMAKE_BINARY_DIR}/_deps/wlroots-build")
set(WLROOTS_INSTALL_DIR "${CMAKE_BINARY_DIR}/_deps/wlroots-install")
set(WLROOTS_LIBRARY
  "${WLROOTS_INSTALL_DIR}/lib/libwlroots-0.20.a"
)
set(WLROOTS_PKGCONFIG_DIR
  "${CMAKE_BINARY_DIR}/_deps/wlroots-pkgconfig"
)

list(JOIN WLROOTS_VENDOR_PKGCONFIG_DIRS ":" _wlroots_pkg_config_path)
if(DEFINED ENV{PKG_CONFIG_PATH} AND NOT "$ENV{PKG_CONFIG_PATH}" STREQUAL "")
  if(_wlroots_pkg_config_path)
    string(APPEND _wlroots_pkg_config_path ":$ENV{PKG_CONFIG_PATH}")
  else()
    set(_wlroots_pkg_config_path "$ENV{PKG_CONFIG_PATH}")
  endif()
endif()

list(JOIN WLROOTS_RUNTIME_LIBRARY_DIRS ":" _wlroots_library_path)
if(DEFINED ENV{LD_LIBRARY_PATH} AND NOT "$ENV{LD_LIBRARY_PATH}" STREQUAL "")
  if(_wlroots_library_path)
    string(APPEND _wlroots_library_path ":$ENV{LD_LIBRARY_PATH}")
  else()
    set(_wlroots_library_path "$ENV{LD_LIBRARY_PATH}")
  endif()
endif()

ExternalProject_Add(wlroots_external
  SOURCE_DIR "${WLROOTS_SOURCE_DIR}"
  BINARY_DIR "${WLROOTS_BUILD_DIR}"
  CONFIGURE_COMMAND
    "${CMAKE_COMMAND}" -E env
    "PKG_CONFIG_PATH=${_wlroots_pkg_config_path}"
    "LD_LIBRARY_PATH=${_wlroots_library_path}"
    "${MESON_EXECUTABLE}" setup
    "<BINARY_DIR>"
    "<SOURCE_DIR>"
    "--prefix=${WLROOTS_INSTALL_DIR}"
    "--libdir=lib"
    "--buildtype=debugoptimized"
    "--wrap-mode=nodownload"
    "-Dexamples=false"
    "-Dwerror=false"
    "-Ddefault_library=static"
    "-Drenderers=gles2"
  BUILD_COMMAND
    "${CMAKE_COMMAND}" -E env
    "PKG_CONFIG_PATH=${_wlroots_pkg_config_path}"
    "LD_LIBRARY_PATH=${_wlroots_library_path}"
    "${MESON_EXECUTABLE}" compile -C "<BINARY_DIR>"
  INSTALL_COMMAND
    "${CMAKE_COMMAND}" -E env
    "LD_LIBRARY_PATH=${_wlroots_library_path}"
    "${MESON_EXECUTABLE}" install -C "<BINARY_DIR>"
  BUILD_ALWAYS TRUE
  BUILD_BYPRODUCTS "${WLROOTS_LIBRARY}"
)

if(WLROOTS_VENDOR_DEPENDENCY_TARGETS)
  add_dependencies(wlroots_external ${WLROOTS_VENDOR_DEPENDENCY_TARGETS})
endif()

list(PREPEND WLROOTS_LINK_DIRECTORIES "${WLROOTS_INSTALL_DIR}/lib")

set(WLROOTS_LINK_STUB_DIR "${CMAKE_BINARY_DIR}/_deps/wlroots-link-stubs")
file(MAKE_DIRECTORY "${WLROOTS_LINK_STUB_DIR}")
if(WLROOTS_RUNTIME_LIBRARY_DIRS)
  list(APPEND WLROOTS_LINK_DIRECTORIES "${WLROOTS_LINK_STUB_DIR}")
endif()

file(MAKE_DIRECTORY
  "${WLROOTS_INSTALL_DIR}/include/wlroots-0.20"
  "${WLROOTS_BUILD_DIR}/protocol"
  "${WLROOTS_PKGCONFIG_DIR}"
)

# Provide configure-time metadata for the selected fallbacks. The paths point
# to their eventual install trees, which are populated before wlroots builds.
if(NOT _flakewm_system_wayland_ok)
  file(MAKE_DIRECTORY
    "${WLROOTS_VENDOR_WAYLAND_PREFIX}/include"
    "${WLROOTS_VENDOR_WAYLAND_PREFIX}/lib"
  )
  file(WRITE "${WLROOTS_LINK_STUB_DIR}/libwayland-server.so"
    "INPUT(${WLROOTS_VENDOR_WAYLAND_PREFIX}/lib/libwayland-server.so)\n"
  )
  file(WRITE "${WLROOTS_LINK_STUB_DIR}/libwayland-client.so"
    "INPUT(${WLROOTS_VENDOR_WAYLAND_PREFIX}/lib/libwayland-client.so)\n"
  )
  foreach(_wayland_library server client)
    file(WRITE
      "${WLROOTS_PKGCONFIG_DIR}/wayland-${_wayland_library}.pc"
"prefix=${WLROOTS_VENDOR_WAYLAND_PREFIX}
libdir=${WLROOTS_LINK_STUB_DIR}
includedir=\${prefix}/include

Name: Wayland ${_wayland_library}
Description: FLAKEWM compatibility fallback
Version: 1.26.0
Libs: -L\${libdir} -lwayland-${_wayland_library}
Cflags: -I\${includedir}
")
  endforeach()
endif()

if(NOT (FLAKEWM_SYSTEM_LIBDRM_FOUND AND
        NOT FLAKEWM_SYSTEM_LIBDRM_VERSION VERSION_LESS "2.4.129"))
  file(MAKE_DIRECTORY
    "${WLROOTS_VENDOR_LIBDRM_PREFIX}/include/libdrm"
    "${WLROOTS_VENDOR_LIBDRM_PREFIX}/lib"
  )
  file(WRITE "${WLROOTS_LINK_STUB_DIR}/libdrm.so"
    "INPUT(${WLROOTS_VENDOR_LIBDRM_PREFIX}/lib/libdrm.so)\n"
  )
  file(WRITE "${WLROOTS_PKGCONFIG_DIR}/libdrm.pc"
"prefix=${WLROOTS_VENDOR_LIBDRM_PREFIX}
libdir=${WLROOTS_LINK_STUB_DIR}
includedir=\${prefix}/include

Name: libdrm
Description: FLAKEWM compatibility fallback
Version: 2.4.134
Libs: -L\${libdir} -ldrm
Cflags: -I\${includedir} -I\${includedir}/libdrm
")
endif()

if(NOT (FLAKEWM_SYSTEM_PIXMAN_FOUND AND
        NOT FLAKEWM_SYSTEM_PIXMAN_VERSION VERSION_LESS "0.46.0"))
  file(MAKE_DIRECTORY
    "${WLROOTS_VENDOR_PIXMAN_PREFIX}/include/pixman-1"
    "${WLROOTS_VENDOR_PIXMAN_PREFIX}/lib"
  )
  file(WRITE "${WLROOTS_LINK_STUB_DIR}/libpixman-1.so"
    "INPUT(${WLROOTS_VENDOR_PIXMAN_PREFIX}/lib/libpixman-1.so)\n"
  )
  file(WRITE "${WLROOTS_PKGCONFIG_DIR}/pixman-1.pc"
"prefix=${WLROOTS_VENDOR_PIXMAN_PREFIX}
libdir=${WLROOTS_LINK_STUB_DIR}
includedir=\${prefix}/include/pixman-1

Name: Pixman
Description: FLAKEWM compatibility fallback
Version: 0.46.4
Libs: -L\${libdir} -lpixman-1
Cflags: -I\${includedir}
")
endif()

# Expose the Meson-built static archive to CMake through the same pkg-config
# contract that upstream wlroots installs. The archive and generated headers
# are produced by wlroots_external before consumers are linked.
file(WRITE "${WLROOTS_PKGCONFIG_DIR}/wlroots-0.20.pc"
"prefix=${WLROOTS_INSTALL_DIR}
exec_prefix=\${prefix}
libdir=\${prefix}/lib
includedir=\${prefix}/include/wlroots-0.20

Name: wlroots
Description: FLAKEWM upstream wlroots build
Version: ${WLROOTS_VERSION}
Requires: wayland-server wayland-client libdrm xkbcommon pixman-1 egl gbm glesv2 lcms2 libudev libseat libdisplay-info libinput xcb xcb-composite xcb-dri3 xcb-errors xcb-ewmh xcb-icccm xcb-present xcb-render xcb-renderutil xcb-res xcb-shm xcb-xfixes xcb-xinput
Cflags: -I\${includedir} -I${WLROOTS_BUILD_DIR}/protocol -DWLR_USE_UNSTABLE
Libs: -L\${libdir} -lwlroots-0.20 -ldl -lm -lrt

wlroots_version=${WLROOTS_VERSION}
wlroots_version_major=${WLROOTS_VERSION_MAJOR}
wlroots_version_minor=${WLROOTS_VERSION_MINOR}
wlroots_version_patch=${WLROOTS_VERSION_PATCH}
wlroots_features=DRM_BACKEND X11_BACKEND LIBINPUT_BACKEND XWAYLAND GLES2_RENDERER GBM_ALLOCATOR UDMABUF_ALLOCATOR SESSION COLOR_MANAGEMENT
")

set(_flakewm_saved_pkg_config_path "$ENV{PKG_CONFIG_PATH}")
if(_flakewm_saved_pkg_config_path)
  set(ENV{PKG_CONFIG_PATH}
    "${WLROOTS_PKGCONFIG_DIR}:${_flakewm_saved_pkg_config_path}"
  )
else()
  set(ENV{PKG_CONFIG_PATH} "${WLROOTS_PKGCONFIG_DIR}")
endif()

pkg_check_modules(FLAKEWM_WLROOTS REQUIRED IMPORTED_TARGET GLOBAL
  "wlroots-0.20=${WLROOTS_VERSION}"
)

set(ENV{PKG_CONFIG_PATH} "${_flakewm_saved_pkg_config_path}")

add_dependencies(PkgConfig::FLAKEWM_WLROOTS wlroots_external)
target_link_directories(PkgConfig::FLAKEWM_WLROOTS INTERFACE
  ${WLROOTS_LINK_DIRECTORIES}
)

if(WLROOTS_RUNTIME_LIBRARY_DIRS)
  list(JOIN WLROOTS_RUNTIME_LIBRARY_DIRS ":" _flakewm_vendor_build_rpath)
  target_link_options(PkgConfig::FLAKEWM_WLROOTS INTERFACE
    "LINKER:-rpath,${_flakewm_vendor_build_rpath}"
  )
endif()

add_library(Wlroots::wlroots ALIAS PkgConfig::FLAKEWM_WLROOTS)
