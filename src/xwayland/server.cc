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
 * Xwayland launcher.
 */

#include <unistd.h>
#include <sysexits.h>

#include <cstdio>
#include <vector>

#ifndef FLAKEWM_XWAYLAND_PATH
#error "FLAKEWM_XWAYLAND_PATH must name the system Xwayland executable"
#endif

int main(int argc, char** argv) {
  std::vector<char*> arguments;
  arguments.reserve(static_cast<size_t>(argc) + 5);
  constexpr char kShell[] = "/bin/sh";
  constexpr char kName[] = "sh";
  constexpr char kCommand[] = "exec \"$0\" \"$@\" -nokeymap";
  arguments.push_back(const_cast<char*>(kName));
  arguments.push_back(const_cast<char*>("-c"));
  arguments.push_back(const_cast<char*>(kCommand));
  arguments.push_back(const_cast<char*>(FLAKEWM_XWAYLAND_PATH));
  for (int index = 1; index < argc; ++index) {
    arguments.push_back(argv[index]);
  }

  arguments.push_back(nullptr);

  execv(kShell, arguments.data());
  std::perror("flakewm-xwayland");
  return EX_UNAVAILABLE;
}
