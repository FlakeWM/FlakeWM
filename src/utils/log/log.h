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
 * Some handy logging utility.
 * Note: Abseil is actually charged for logging.
 */

#ifndef SRC_UTILS_LOG_LOG_H_
#define SRC_UTILS_LOG_LOG_H_

#include <absl/base/log_severity.h>

namespace flakewm {
namespace utils {

using LogLevel = absl::LogSeverity;

// Initializes Abseil logging and routes wlroots logs through it.
void InitializeLogging(LogLevel minimum_level);
void SetLoggingLevel(LogLevel minimum_level);

}  // namespace utils
}  // namespace flakewm

#endif  // SRC_UTILS_LOG_LOG_H_
