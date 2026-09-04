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
 * This file provides some misc utilities.
 */

#include "src/utils/misc/misc.h"

#include <absl/log/absl_log.h>
#include <signal.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sysexits.h>
#include <unistd.h>

#include <optional>
#include <string>

namespace flakewm {
namespace utils {

// Referenced from Wayfire. It attempts to drop the process privileges if the
// process is started as root.
// It gives up both group & user privileges, and also checks if the process can
// restore root privileges after dropping them. If it can, then dropping
// failed, and the WM will reject to start due to security concerns.
bool Misc::DropProcessPrivilegePermissions() {
  if ((getuid() == 0) || (getgid() == 0)) {
    // Set gid & uid in correct order
    if ((setgid(getgid()) != 0) || (setuid(getuid()) != 0)) {
      // Unable to drop
      return false;
    }
  }

  if ((setgid(0) != -1) || (setuid(0) != -1)) {
    // Unable to frop root for still abling to restore it after setuid
    // Reject to start.
    return false;
  }

  return true;
}

// Also from Wayfire, this function selects a valid wayland socket.
std::optional<std::string> Misc::SelectWaylandSocket(wl_display* display) {
  for (int i = 1; i <= 32; i++) {
    auto name_gen = "wayland-" + std::to_string(i);
    if (wl_display_add_socket(display, name_gen.c_str()) >= 0) {
      return name_gen;
    }
  }

  return {};
}

// Referenced from Wayfire, handles fatal signals.
void Misc::HandleSignal(int signal) {
  std::string err;
  int ex_code = EX_SOFTWARE;
  switch (signal) {
    case SIGSEGV: {
      err = "Segmentation fault";
      ex_code = EX_SOFTWARE;
      break;
    }

    case SIGFPE: {
      err = "Floating-point exception";
      ex_code = EX_SOFTWARE;
      break;
    }

    case SIGABRT: {
      err = "Fatal error(SIGABRT)";
      ex_code = EX_SOFTWARE;
      break;
    }

    case SIGINT: {
      // wf::get_core().shutdown();
      err = "SIGINT";
      return;
    }

    case SIGTERM: {
      // wf::get_core().shutdown();
      err = "SIGTERM";
      return;
    }

    default: {
      err = "Unknown";
      ex_code = EX_SOFTWARE;
    }
  }

  ABSL_LOG(ERROR) << "Fatal error: " << err;
  std::_Exit(ex_code);
}

}  // namespace utils
}  // namespace flakewm
