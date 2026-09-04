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
 * This file is the public class of core compositor, it is implemented in
 * compositor_private.h/cc.
 */

#ifndef SRC_CORE_COMPOSITOR_COMPOSITOR_H_
#define SRC_CORE_COMPOSITOR_COMPOSITOR_H_

#include <memory>

#include "src/utils/args_handler/args_handler.h"

namespace flakewm {
namespace core {

class CompositorPrivate;

class Compositor final {
 public:
  Compositor();
  ~Compositor();

  Compositor(const Compositor&) = delete;
  Compositor& operator=(const Compositor&) = delete;

  bool Start(const flakewm::utils::StartupArgs& startup_args);
  void Run();
  void Stop();

 private:
  struct PrivateDeleter {
    void operator()(CompositorPrivate* compositor) const;
  };

  std::unique_ptr<CompositorPrivate, PrivateDeleter> private_;
};

}  // namespace core
}  // namespace flakewm

#endif  // SRC_CORE_COMPOSITOR_COMPOSITOR_H_
