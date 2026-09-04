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

#ifndef SRC_UTILS_ARGS_HANDLER_ARGS_HANDLER_H_
#define SRC_UTILS_ARGS_HANDLER_ARGS_HANDLER_H_

#include <absl/base/log_severity.h>

#include <string>

namespace flakewm {
namespace utils {

struct StartupArgs {
  absl::LogSeverity info_level = absl::LogSeverity::kInfo;
  bool nested = false;
  bool disable_xwayland = false;
  std::string process = "";
  bool exit_flag = false;
};

class ArgsHandler {
 public:
  explicit ArgsHandler(char** argv);
  ~ArgsHandler() = default;

  static void PrintHelp();
  static void PrintVersion();
  StartupArgs GetArgs();

 private:
  StartupArgs args_;
};

}  // namespace utils
}  // namespace flakewm

#endif  // SRC_UTILS_ARGS_HANDLER_ARGS_HANDLER_H_
