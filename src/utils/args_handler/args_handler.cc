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
 * This file provides handles the startup arguments.
 */

#include "src/utils/args_handler/args_handler.h"

#include <absl/log/absl_log.h>

#include <iostream>
#include <string>

namespace flakewm {
namespace utils {

// Initializes Argument handler by parsing the command line arguments
// to configurations.
ArgsHandler::ArgsHandler(char** argv) {
  for (int i = 1; argv[i] != nullptr; ++i) {
    std::string cur = argv[i];
    if (cur == "-h" || cur == "--help") {
      PrintHelp();
      args_.exit_flag = true;
      return;
    }

    if (cur == "-v" || cur == "--version") {
      PrintVersion();
      args_.exit_flag = true;
      return;
    }

    if (cur == "-d" || cur == "--debug") {
      args_.info_level = absl::LogSeverity::kInfo;
      continue;
    }

    if (cur == "-w" || cur == "--warning") {
      args_.info_level = absl::LogSeverity::kWarning;
      continue;
    }

    if (cur == "-e" || cur == "--error") {
      args_.info_level = absl::LogSeverity::kError;
      continue;
    }

    if (cur == "-n" || cur == "--nested") {
      args_.nested = true;
      continue;
    }

    if (cur == "-nx" || cur == "--noxwayland") {
      args_.disable_xwayland = true;
      continue;
    }

    if (cur == "-s" || cur == "--session") {
      if (argv[i + 1] != nullptr) {
        args_.process = argv[i + 1];
        i += 1;
      } else {
        ABSL_LOG(ERROR) << "Missing argument for session.";
        args_.exit_flag = true;
        return;
      }
      continue;
    }
  }
}

// Simply returns the parsed arguments as a StartupArgs struct.
StartupArgs ArgsHandler::GetArgs() { return args_; }

// Prints usage help.
void ArgsHandler::PrintHelp() {
  std::cout << "Usage: flakewm [options] [command]\n";
  std::cout << "  -h, --help               Show this message & quit.\n";
  std::cout << "  -v, --version            Show the version number & quit.\n";
  std::cout << "  -d, --debug              Set log level to info.\n";
  std::cout << "  -w, --warning            Set log level to warning.\n";
  std::cout << "  -e, --error              Set log level to error.\n";
  std::cout << "  -n, --nested             Start WM in nested mode.\n";
  std::cout << "  -nx, --noxwayland        Disable XWayland support.\n";
  std::cout << "  -s, --session <process>  Run session on startup\n";
}

// Prints the version number of FLAKEWM.
void ArgsHandler::PrintVersion() {
  std::cout << "FLAKEWM version " << FLAKEWM_VERSION << '\n';
}

}  // namespace utils
}  // namespace flakewm
