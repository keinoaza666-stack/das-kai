// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <vector>

namespace das::sync {

// Kaiser-windowed sinc interpolator for reading a signal at fractional
// positions. Used to resample by ratios within a few hundred ppm of 1, so the
// kernel is a plain fractional-delay filter with its cutoff at Nyquist.
//
// At an integer position (fraction 0) the kernel is a unit impulse, so audio
// passes through bit-exactly when no correction is needed.
class SincInterpolator final {
public:
  static constexpr int kTaps = 64;
  static constexpr int kHalfTaps = kTaps / 2;
  static constexpr int kPhases = 1024;
  static constexpr double kKaiserBeta = 9.0;

  // Builds the coefficient table. Allocates; construct off the audio thread.
  SincInterpolator();

  // Interpolates one value at (base + fraction), where base indexes `data`
  // with the given stride. Reads data[(base - kHalfTaps + 1) * stride] through
  // data[(base + kHalfTaps) * stride]; the caller guarantees they exist.
  [[nodiscard]] float at(const float* data, std::size_t base, std::size_t stride,
                         double fraction) const noexcept;

private:
  std::vector<float> table_; // (kPhases + 1) rows of kTaps coefficients
};

} // namespace das::sync
