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

#include "src/view/ssd/ssd_buffer/ssd_buffer.h"

#include <drm_fourcc.h>

#include <new>

namespace flakewm {
namespace view {

const wlr_buffer_impl SsdBuffer::kBufferImpl = {
    .destroy = Destroy,
    .get_dmabuf = nullptr,
    .get_shm = nullptr,
    .begin_data_ptr_access = BeginDataAccess,
    .end_data_ptr_access = EndDataAccess,
};

SsdBuffer* SsdBuffer::Create(int width, int height) {
  if (width <= 0 || height <= 0) {
    return nullptr;
  }

  auto* buffer = new (std::nothrow) SsdBuffer(width, height);
  if (buffer == nullptr || buffer->image_.isNull()) {
    delete buffer;
    return nullptr;
  }
  wlr_buffer_init(&buffer->handle_, &kBufferImpl, width, height);
  return buffer;
}

SsdBuffer::SsdBuffer(int width, int height)
    : image_(width, height, QImage::Format_ARGB32_Premultiplied) {
  image_.fill(Qt::transparent);
}

wlr_buffer* SsdBuffer::Handle() { return &handle_; }

QImage& SsdBuffer::Image() { return image_; }

void SsdBuffer::Destroy(wlr_buffer* buffer) {
  auto* ssd_buffer = reinterpret_cast<SsdBuffer*>(buffer);
  wlr_buffer_finish(buffer);
  delete ssd_buffer;
}

bool SsdBuffer::BeginDataAccess(wlr_buffer* buffer, uint32_t flags, void** data,
                                uint32_t* format, size_t* stride) {
  if ((flags & WLR_BUFFER_DATA_PTR_ACCESS_WRITE) != 0) {
    return false;
  }

  auto* ssd_buffer = reinterpret_cast<SsdBuffer*>(buffer);
  *data = ssd_buffer->image_.bits();
  *format = DRM_FORMAT_ARGB8888;
  *stride = static_cast<size_t>(ssd_buffer->image_.bytesPerLine());
  return true;
}

void SsdBuffer::EndDataAccess(wlr_buffer*) {}

}  // namespace view
}  // namespace flakewm
