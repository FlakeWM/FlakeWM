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

cmake_minimum_required(VERSION 3.25)

project(FlakeWM
  VERSION 0.1.0
  DESCRIPTION "[WIP] Simple Wayland Compositor based on Waylib"
  LANGUAGES C CXX
)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)

find_package(Qt6
  6.8 REQUIRED
  COMPONENTS
    Core
    Gui
    Qml
    Quick
)

# We allow you to use your system Waylib package by manually enabling
# FLAKEWM_USE_SYSTEM_WAYLIB, but it is your responsibility to ensure that
# your system Waylib package is compatible with FlakeWM.
option(FLAKEWM_USE_SYSTEM_WAYLIB
  "Use a system-installed Waylib package"
  OFF
)


if(FLAKEWM_USE_SYSTEM_WAYLIB)
  # If using system Waylib, we just need to find it.
  find_package(Waylib REQUIRED COMPONENTS Server)
else()
  # If using vendored Waylib, then AddVendorWaylib shall handle the
  # path of submodule problem for you.
  include(cmake/AddVendorWaylib.cmake)
endif()

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(flakewm
  src/main.cc
)

if(TARGET waylibserverplugin)
  # While using vendored Waylib, it is expected that the QML plugin
  # is NOT in the system PATH.
  # We explicitly add a dependency & import path to resolve this issue.
  add_dependencies(flakewm waylibserverplugin)
  get_target_property(FLAKEWM_WAYLIB_QML_IMPORT_PATH waylibserver
    QT_QML_MODULE_OUTPUT_DIRECTORY
  )
  
  target_compile_definitions(flakewm PRIVATE
    FLAKEWM_WAYLIB_QML_IMPORT_PATH="${FLAKEWM_WAYLIB_QML_IMPORT_PATH}"
  )
endif()

qt_add_qml_module(flakewm
  URI FlakeWM
  VERSION 1.0
  IMPORTS Waylib.Server
  SOURCES
    src/core/compositor/compositor.cc
    src/core/compositor/compositor.h
  QML_FILES
    src/qml/Main.qml
)

target_link_libraries(flakewm PRIVATE
  Qt6::Core
  Qt6::Gui
  Qt6::Qml
  Qt6::Quick
  Waylib::WaylibServer
)

target_include_directories(flakewm PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/"
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/core/compositor"
)

install(TARGETS flakewm
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
