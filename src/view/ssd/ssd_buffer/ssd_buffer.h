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
 * QImage-backed wlroots buffer used by server-side decorations.
 */

#ifndef SRC_VIEW_SSD_SSD_BUFFER_SSD_BUFFER_H_
#define SRC_VIEW_SSD_SSD_BUFFER_SSD_BUFFER_H_

#include <QImage>
#include <cstddef>
#include <cstdint>

#include "src/wlr_wrapper/wlroots.h"

namespace flakewm {
namespace view {

class SsdBuffer final {
 public:
  static SsdBuffer* Create(int width, int height);

  wlr_buffer* Handle();
  QImage& Image();

 private:
  SsdBuffer(int width, int height);
  ~SsdBuffer() = default;

  static void Destroy(wlr_buffer* buffer);
  static bool BeginDataAccess(wlr_buffer* buffer, uint32_t flags, void** data,
                              uint32_t* format, size_t* stride);
  static void EndDataAccess(wlr_buffer* buffer);

  static const wlr_buffer_impl kBufferImpl;

  wlr_buffer handle_ = {};
  QImage image_;
};

}  // namespace view
}  // namespace flakewm

#endif  // SRC_VIEW_SSD_SSD_BUFFER_SSD_BUFFER_H_
