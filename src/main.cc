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
 * Main entry of FlakeWM.
 */

#include <absl/log/absl_log.h>

#include <csignal>
#include <cstdlib>
#include <exception>

#include "src/core/compositor/compositor.h"
#include "src/utils/args_handler/args_handler.h"
#include "src/utils/log/log.h"

int main(int argc, char* argv[]) {
  static_cast<void>(argc);

  // A compositor must NOT terminate when a cilent disappears while data
  // is still being processed in its sockets.
  std::signal(SIGPIPE, SIG_IGN);
  std::signal(SIGCHLD, SIG_IGN);

  // Initialize logging before handling arguments, so argument errors can also
  // be logged.
  flakewm::utils::InitializeLogging(flakewm::utils::LogLevel::kInfo);

  try {
    // Parse startup arguments. Help and version requests exit here.
    flakewm::utils::ArgsHandler args_handler(argv);
    const flakewm::utils::StartupArgs startup_args = args_handler.GetArgs();
    if (startup_args.exit_flag) {
      return EXIT_SUCCESS;
    }

    flakewm::utils::SetLoggingLevel(startup_args.info_level);
    ABSL_LOG(INFO) << "Starting FlakeWM v" << FLAKEWM_VERSION << '.';

    // Start the compositor core, then enter the Wayland event loop.
    flakewm::core::Compositor compositor;
    if (!compositor.Start(startup_args)) {
      ABSL_LOG(ERROR) << "Failed to start FlakeWM, halted!";
      return EXIT_FAILURE;
    }

    compositor.Run();
    ABSL_LOG(INFO) << "Shutting down FlakeWM...";
    return EXIT_SUCCESS;
  } catch (const std::exception& exception) {
    // Nothing should escape from the compositor core.
    ABSL_LOG(ERROR) << "Unhandled exception: " << exception.what();
  } catch (...) {
    ABSL_LOG(ERROR) << "Unhandled non-standard exception";
  }

  return EXIT_FAILURE;
}
