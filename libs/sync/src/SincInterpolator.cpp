// SPDX-License-Identifier: MIT
#include <das/sync/SincInterpolator.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace das::sync {
namespace {

double besselI0(const double x) noexcept {
  // Power series; converges quickly for the beta values used here.
  double sum = 1.0;
  double term = 1.0;
  const auto half = x * 0.5;
  for (int k = 1; k < 64; ++k) {
    term *= (half / k) * (half / k);
    sum += term;
    if (term < sum * 1.0e-17) break;
  }
  return sum;
}

double kernel(const double x) noexcept {
  constexpr auto half = static_cast<double>(SincInterpolator::kHalfTaps);
  if (std::abs(x) >= half) return 0.0;
  const auto ratio = x / half;
  const auto window = besselI0(SincInterpolator::kKaiserBeta * std::sqrt(1.0 - ratio * ratio)) /
                      besselI0(SincInterpolator::kKaiserBeta);
  const auto sinc = x == 0.0 ? 1.0 : std::sin(std::numbers::pi * x) / (std::numbers::pi * x);
  return sinc * window;
}

} // namespace

SincInterpolator::SincInterpolator()
    : table_(static_cast<std::size_t>(kPhases + 1) * kTaps) {
  for (int phase = 0; phase <= kPhases; ++phase) {
    const auto fraction = static_cast<double>(phase) / kPhases;
    double coefficients[kTaps] {};
    double sum {};
    for (int tap = 0; tap < kTaps; ++tap) {
      // Tap t multiplies data[base - kHalfTaps + 1 + t]; its distance from the
      // interpolation point is (t - kHalfTaps + 1) - fraction.
      coefficients[tap] = kernel(static_cast<double>(tap - kHalfTaps + 1) - fraction);
      sum += coefficients[tap];
    }
    // Unity DC gain for every phase so slow position changes cannot modulate level.
    for (int tap = 0; tap < kTaps; ++tap)
      table_[static_cast<std::size_t>(phase) * kTaps + tap] =
          static_cast<float>(coefficients[tap] / sum);
  }
}

float SincInterpolator::at(const float* const data, const std::size_t base,
                           const std::size_t stride, const double fraction) const noexcept {
  const auto scaled = std::clamp(fraction, 0.0, 1.0) * kPhases;
  const auto phase = std::min(static_cast<int>(scaled), kPhases - 1);
  const auto blend = scaled - phase;
  const auto* const lower = table_.data() + static_cast<std::size_t>(phase) * kTaps;
  const auto* const upper = lower + kTaps;
  const auto* sample = data + (base - kHalfTaps + 1) * stride;

  if (blend == 0.0) {
    double acc {};
    for (int tap = 0; tap < kTaps; ++tap, sample += stride)
      acc += static_cast<double>(lower[tap]) * static_cast<double>(*sample);
    return static_cast<float>(acc);
  }

  double accLower {};
  double accUpper {};
  for (int tap = 0; tap < kTaps; ++tap, sample += stride) {
    const auto value = static_cast<double>(*sample);
    accLower += static_cast<double>(lower[tap]) * value;
    accUpper += static_cast<double>(upper[tap]) * value;
  }
  return static_cast<float>(accLower + (accUpper - accLower) * blend);
}

} // namespace das::sync
