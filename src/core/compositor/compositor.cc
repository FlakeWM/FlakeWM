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

#include "src/core/compositor/compositor.h"

#include "src/core/compositor_private/compositor_private.h"

namespace flakewm {
namespace core {

Compositor::Compositor() : private_(new CompositorPrivate()) {}

Compositor::~Compositor() = default;

void Compositor::PrivateDeleter::operator()(
    CompositorPrivate* compositor) const {
  delete compositor;
}

bool Compositor::Start(const utils::StartupArgs& startup_args) {
  return private_->Start(startup_args);
}

void Compositor::Run() { private_->Run(); }

void Compositor::Stop() { private_->Stop(); }

}  // namespace core
}  // namespace flakewm
