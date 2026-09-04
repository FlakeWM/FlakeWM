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

#include "src/utils/log/log.h"

extern "C" {
#include <wlr/util/log.h>
}

#include <absl/log/absl_log.h>
#include <absl/log/globals.h>
#include <absl/log/initialize.h>

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

namespace flakewm {
namespace utils {
namespace {

std::string FormatMessage(const char* format, va_list arguments) {
  va_list arguments_copy;
  va_copy(arguments_copy, arguments);
  const int length = std::vsnprintf(nullptr, 0, format, arguments_copy);
  va_end(arguments_copy);

  if (length < 0) {
    return format;
  }

  std::vector<char> buffer(static_cast<std::size_t>(length) + 1);
  va_copy(arguments_copy, arguments);
  std::vsnprintf(buffer.data(), buffer.size(), format, arguments_copy);
  va_end(arguments_copy);

  std::string message(buffer.data(), static_cast<std::size_t>(length));
  while (!message.empty() && message.back() == '\n') {
    message.pop_back();
  }
  return message;
}

void HandleWlrootsLog(wlr_log_importance importance, const char* format,
                      va_list arguments) {
  const std::string message = FormatMessage(format, arguments);
  switch (importance) {
    case WLR_ERROR:
      ABSL_LOG(ERROR) << "wlroots: " << message;
      break;
    case WLR_INFO:
      ABSL_LOG(INFO) << "wlroots: " << message;
      break;
    case WLR_DEBUG:
      ABSL_LOG(INFO) << "wlroots [debug]: " << message;
      break;
    case WLR_SILENT:
      break;
  }
}

}  // namespace

void InitializeLogging(LogLevel minimum_level) {
  absl::InitializeLog();
  SetLoggingLevel(minimum_level);
  wlr_log_init(WLR_DEBUG, HandleWlrootsLog);
}

void SetLoggingLevel(LogLevel minimum_level) {
  absl::SetMinLogLevel(static_cast<absl::LogSeverityAtLeast>(minimum_level));
  absl::SetStderrThreshold(minimum_level);
}

}  // namespace utils
}  // namespace flakewm
