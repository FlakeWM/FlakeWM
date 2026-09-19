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
 * Separable Gaussian used by both backdrop blur backends.
 */

#ifndef SRC_RENDER_BLUR_KERNEL_H_
#define SRC_RENDER_BLUR_KERNEL_H_

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace flakewm {
namespace render {

// One weighted sample of the kernel.  `displacement` is in pixels and is
// deliberately allowed to be fractional: both backends feed it to a bilinear
// sampler, and a fractional displacement deposits its weight on the two
// neighbouring texels in exactly the ratio the Gaussian asks for.
struct BlurTap {
  float displacement;
  float weight;
};

// The kernel used to be five fixed masses at 0, +/-1.3846 and +/-3.2308
// multiplied by the requested offset.  Those weights are the linear-sampling
// reconstruction of a 9-tap Gaussian at one-pixel spacing, and they reconstruct
// that Gaussian only while the taps stay one pixel apart.  Scaling the spacing
// does not widen the Gaussian, it turns the kernel into a sparse comb: at
// offset 3 the blurred image is 0.227 parts of the untouched source plus loud
// ghosts 4 px and 10 px away from every edge, which reads as hard blocky
// echoes.  So the taps are rebuilt for the requested radius instead -- a
// Gaussian sampled at every whole pixel, with each adjacent pair of samples
// folded into a single fractional tap.
//
// What "the requested radius" means comes from gxde-wlcom, the reference
// compositor for these protocols.  It does not convolve with a fixed kernel: it
// builds a three-level pyramid, blurring with a 5-tap X on the way down and an
// 8-tap ring on the way up (src/effect/blur.c plus the shaders in
// src/effect/shaders/), halving the resolution each level and scaling every tap
// by the client's `offset`.  Its own calculate_blur_radius() reports that as
// `offset * 2^(iterations + 1)` pixels, and the clients choose `offset` against
// *that*: gxde-dock, gxde-launcher and dde-grand-search all call
// set_strength(300), and treeland's default level is 2.6
// (src/scene/scene.c: ky_scene_node_reset_blur_level).
//
// So an `offset` is a pyramid tap scale, not a pixel radius, and it has to be
// converted.  The pyramid is not worth rebuilding -- one separable Gaussian is
// smoother for the same cost -- but its impulse response is worth matching, and
// it is the sigma below: the pyramid's offset-independent resampling blur in
// quadrature with a term proportional to gxde-wlcom's radius.
//
//   offset   pyramid   this file
//     0.3      1.98      1.98
//     0.6      2.37      2.44
//     1.0      2.97      2.91
//     1.5      3.46      3.84
//     2.0      4.85      4.76
//     2.6      5.59      6.11
//     3.0      6.71      6.97
//
// Within ~11% over the range the clients actually use, which is well inside the
// step between one client's setting and the next.
//
// One separable Gaussian covers that whole range, but a single kernel cannot:
// its tap count is proportional to sigma, so a radius of 36 px -- which gxde's
// upper strength levels do ask for -- would need over a hundred taps per axis
// per pixel.  Instead the source is halved a few times, the Gaussian is applied
// once at the coarse level, and the result is expanded back.  Blurring a
// downsampled image is the standard way to buy a wide radius cheaply, and it
// costs nothing in smoothness as long as the coarse level is still blurred by
// more than a couple of its own pixels -- which is what `PlanBlur` enforces
// below, by refusing to halve any further once the remainder would drop under
// `kBlurCoarseSigma`.  A sigma small enough to fit in one pass therefore still
// takes exactly one pass, and the chain only appears when it pays for itself.
constexpr float kBlurResampleSigma = 1.86F;
constexpr float kBlurRadiusToSigma = 0.14F;
constexpr int kBlurIterations =
    3;  // gxde-wlcom kde_blur.c: set_blur_level(., 3, .)
constexpr float kBlurRadiusInSigmas = 3.0F;

// Converts a protocol `offset` into the sigma the kernel is built for.
// `iterations` is gxde-wlcom's pyramid depth, which its UKUI level table varies
// per level; every other caller uses the default.
inline float SigmaForBlurOffset(float offset,
                                int iterations = kBlurIterations) {
  const float radius = offset * static_cast<float>(1 << (iterations + 1));
  const float proportional = kBlurRadiusToSigma * radius;
  return std::sqrt(kBlurResampleSigma * kBlurResampleSigma +
                   proportional * proportional);
}

// How many times the source may be halved before the Gaussian runs.  Each level
// costs a quarter of the one above it, so five of them add up to under a third
// of a full-resolution pass while reaching a sigma of ~66 px on their own.
constexpr int kBlurMaxLevels = 5;

// Halving stops paying once the sigma left over is only a couple of pixels at
// the reduced level: past that the bilinear expand shows its texel grid instead
// of hiding it, which is the granularity this whole file exists to avoid.
constexpr float kBlurCoarseSigma = 2.0F;

// The chain is not free.  Each halving averages a 2x2 block, and each expand
// back up is a bilinear tent one level wide.  Measured in pixels of the
// *original* source, level k's box spans 2*2^k and its tent 2*2^(k+1), so their
// variances 4^k/3 and 2*4^k/3 add to exactly 4^k -- and levels 0..n-1 together
// contribute (4^n - 1) / 3.  A sigma of 6 px, for instance, arrives as a 1 px
// widening chain plus a 3 px Gaussian one level down.
//
// `PlanBlur` subtracts that widening back out of the requested variance, so the
// chain makes a blur cheaper without making it stronger.
struct BlurPlan {
  int levels;   // 2x reductions applied before the Gaussian; 0 = a single pass
  float sigma;  // Gaussian sigma in pixels of the reduced level `levels`
};

inline BlurPlan PlanBlur(float sigma) {
  int levels = 0;
  while (levels < kBlurMaxLevels &&
         sigma / static_cast<float>(1 << (levels + 1)) >= kBlurCoarseSigma) {
    ++levels;
  }
  float scale_squared = 1.0F;  // 4^levels
  for (int i = 0; i < levels; ++i) {
    scale_squared *= 4.0F;
  }
  const float resample_variance = (scale_squared - 1.0F) / 3.0F;
  const float coarse_variance =
      std::max(sigma * sigma - resample_variance, 0.0F) / scale_squared;
  return {levels, std::sqrt(coarse_variance)};
}

// A Gaussian is dead by 3 sigma; the cap keeps a hostile offset from asking for
// hundreds of taps.  Must stay <= kBlurMaxTaps.
constexpr int kBlurMaxRadius = 32;
constexpr size_t kBlurMaxTaps = 64;

constexpr size_t kBlurMaxTapCount =
    1 + 2 * static_cast<size_t>((kBlurMaxRadius + 1) / 2);

static_assert(kBlurMaxTapCount <= kBlurMaxTaps,
              "kBlurMaxTaps must hold the widest kernel");

// Builds the one-dimensional kernel for a per-axis `sigma` in pixels,
// normalised so the weights sum to one.  Returns at least the central tap.
inline std::vector<BlurTap> BuildBlurTaps(float sigma) {
  constexpr float kMinSigma = 1.0F / kBlurRadiusInSigmas;
  constexpr float kMaxSigma =
      static_cast<float>(kBlurMaxRadius) / kBlurRadiusInSigmas;
  sigma = std::clamp(sigma, kMinSigma, kMaxSigma);
  const int radius = std::min(
      static_cast<int>(std::ceil(sigma * kBlurRadiusInSigmas)), kBlurMaxRadius);

  std::array<float, kBlurMaxRadius + 1> weights = {};
  float total = 0.0F;
  for (int i = 0; i <= radius; ++i) {
    const float distance = static_cast<float>(i) / sigma;
    weights[static_cast<size_t>(i)] = std::exp(-0.5F * distance * distance);
    total += i == 0 ? weights[0] : 2.0F * weights[static_cast<size_t>(i)];
  }

  std::vector<BlurTap> taps;
  taps.reserve(static_cast<size_t>(radius) + 2);
  taps.push_back({0.0F, weights[0] / total});
  for (int i = 1; i <= radius; i += 2) {
    const bool paired = i + 1 <= radius;
    const float mass = weights[static_cast<size_t>(i)] +
                       (paired ? weights[static_cast<size_t>(i) + 1] : 0.0F);
    // A single fetch at `i + fraction` splits its mass between the whole-pixel
    // positions i and i + 1 in that ratio, so the pair costs one sample.
    const float fraction =
        paired ? weights[static_cast<size_t>(i) + 1] / mass : 0.0F;
    const float displacement = static_cast<float>(i) + fraction;
    taps.push_back({displacement, mass / total});
    taps.push_back({-displacement, mass / total});
  }
  return taps;
}

}  // namespace render
}  // namespace flakewm

#endif  // SRC_RENDER_BLUR_KERNEL_H_
