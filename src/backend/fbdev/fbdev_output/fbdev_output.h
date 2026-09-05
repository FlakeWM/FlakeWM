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
 * Originally copyright by (C) 2024 KylinSoft Co., Ltd.
 * Original license: GPL-1.0-or-later, see Open Kylin Wayland Compositor.
 * Redistributed with GPL-3.0-or-later.
 * Original code is modified to adapt C++ and Wlroots 0.20.2.
 */

#ifndef SRC_BACKEND_FBDEV_FBDEV_OUTPUT_FBDEV_OUTPUT_H_
#define SRC_BACKEND_FBDEV_FBDEV_OUTPUT_FBDEV_OUTPUT_H_

#include <linux/fb.h>

#include <cstddef>
#include <cstdint>
#include <string>

#include "src/wlr_wrapper/wlroots.h"

namespace flakewm {
namespace backend {
namespace fbdev {

class FbdevBackend;

class FbdevOutput final {
 public:
  static FbdevOutput* Create(FbdevBackend* backend, const std::string& device,
                             int index);

  FbdevOutput(const FbdevOutput&) = delete;
  FbdevOutput& operator=(const FbdevOutput&) = delete;

  wlr_output* Handle();
  static bool IsHandle(const wlr_output* output);
  bool Reenable();
  void Offscreen();

 private:
  struct ScreenInfo {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t width_mm = 0;
    uint32_t height_mm = 0;
    uint32_t bits_per_pixel = 0;
    uint32_t pixel_format = 0;
    int32_t refresh = 60000;
    std::size_t buffer_length = 0;
    std::size_t line_length = 0;
    fb_var_screeninfo current = {};
    char description[16] = {};
  };

  struct Mode {
    wlr_output_mode handle = {};
    fb_var_screeninfo screen = {};
  };

  FbdevOutput(FbdevBackend* backend, std::string device, int index);
  ~FbdevOutput();

  bool Initialize();
  bool Open(ScreenInfo* info);
  void Close();
  bool QueryScreenInfo(ScreenInfo* info) const;
  bool SetScreenInfo(fb_var_screeninfo* info, bool test_only) const;
  bool WakeScreen() const;
  bool SetDpms(bool enabled);
  bool Enable();
  bool Disable(bool clear_screen);
  bool Map();
  void Unmap();
  bool CopyBuffer(const wlr_output_state* state);
  void UpdateFrameDelay(int32_t refresh);

  static FbdevOutput* FromHandle(wlr_output* output);
  static const wlr_output_impl* Implementation();
  static bool Test(wlr_output* output, const wlr_output_state* state);
  static bool Commit(wlr_output* output, const wlr_output_state* state);
  static void Destroy(wlr_output* output);
  static int SendFrame(void* data);

  FbdevBackend* backend_ = nullptr;
  std::string device_;
  int index_ = 0;
  wlr_output output_ = {};
  Mode mode_ = {};
  ScreenInfo screen_ = {};
  wl_event_source* frame_timer_ = nullptr;
  void* framebuffer_ = nullptr;
  int fd_ = -1;
  int frame_delay_ms_ = 16;
  uint32_t pending_present_commit_ = 0;
  bool output_initialized_ = false;
  bool dpms_supported_ = true;
  bool resume_enabled_ = false;
};

}  // namespace fbdev
}  // namespace backend
}  // namespace flakewm

#endif  // SRC_BACKEND_FBDEV_FBDEV_OUTPUT_FBDEV_OUTPUT_H_
