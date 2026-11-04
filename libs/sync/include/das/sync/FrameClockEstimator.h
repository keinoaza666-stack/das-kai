// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

namespace das::sync {

// Turns jittery (frame, time) observations from the audio thread into a
// smooth linear clock model:
//
//   time(frame) = anchorTimeNs + (frame - anchorFrame) * periodNs
//
// The producer feeds one observation per processed block: the frame position
// at the end of the block (in 48 kHz ring frames, possibly fractional) and the
// monotonic time at which the block was handed to the plug-in. The model is an
// exponentially weighted least-squares line through recent observations: the
// slope is the DAW clock's real speed, the line at the newest frame is the
// best estimate of when it was processed. The averaging window grows from one
// second to kWindowNs while locking, so the model settles quickly and then
// ignores scheduling jitter.
//
// Allocation-free and lock-free so it can run on the real-time audio thread.
class FrameClockEstimator final {
public:
  enum class Update : std::uint8_t {
    anchored, // model (re)started from this observation; consumers must resync
    tracked,  // model refined
    ignored   // observation skipped (no progress, or a one-off outlier)
  };

  // An observation further than this from the prediction is a discontinuity
  // (engine restart, dropout, offline render) and re-anchors immediately.
  static constexpr double kResetThresholdNs = 150.0e6;
  // While locking, an observation this far off means the host was still
  // pre-rolling (processing blocks faster than real time): start over from it
  // so those blocks cannot bend the line.
  static constexpr double kLockingResetThresholdNs = 50.0e6;
  // Real audio clocks differ by tens of ppm; anything beyond this is clamped
  // so the consumer never pitches audibly.
  static constexpr double kMaxDeviation = 2.0e-3;
  static constexpr double kWindowNs = 30.0e9;
  static constexpr double kMinimumWindowNs = 1.0e9;
  static constexpr double kLockingDurationNs = 4.0e9;
  // After locking, observations further than max(kOutlierFloorNs,
  // kOutlierJitterFactor x typical jitter) from the line are skipped; this
  // many in a row means the stream really jumped (lost blocks) and re-anchors.
  static constexpr double kOutlierFloorNs = 30.0e6;
  static constexpr double kOutlierJitterFactor = 6.0;
  static constexpr int kOutliersBeforeReanchor = 4;

  void reset(double nominalPeriodNs) noexcept;
  Update observe(double frame, std::int64_t timeNs) noexcept;

  [[nodiscard]] bool hasModel() const noexcept { return initialised_; }
  [[nodiscard]] double anchorFrame() const noexcept { return lastFrame_; }
  [[nodiscard]] std::int64_t anchorTimeNs() const noexcept;
  [[nodiscard]] double periodNs() const noexcept { return nominalPeriodNs_ + slope_; }
  [[nodiscard]] double nominalPeriodNs() const noexcept { return nominalPeriodNs_; }
  // Positive when the DAW clock runs fast relative to the system clock.
  [[nodiscard]] double driftPpm() const noexcept;
  [[nodiscard]] bool isLocked() const noexcept {
    return initialised_ && trackedForNs_ >= kLockingDurationNs;
  }

private:
  void anchor(double frame, std::int64_t timeNs) noexcept;
  void fit() noexcept;

  double nominalPeriodNs_ {1.0e9 / 48000.0};
  // Reference point of the current anchor; residuals are measured against
  // the nominal line through it, which keeps every sum small.
  double referenceFrame_ {};
  std::int64_t referenceTimeNs_ {};
  double lastFrame_ {};
  // Weighted sums over (u, v): u = frames before the newest observation
  // (<= 0), v = time minus the nominal line, in ns.
  double weight_ {};
  double sumU_ {};
  double sumUU_ {};
  double sumV_ {};
  double sumUV_ {};
  double intercept_ {}; // v of the fitted line at the newest frame
  double slope_ {};     // ns per frame beyond nominal
  double trackedForNs_ {};
  double jitterNs_ {2.0e6};
  int outliers_ {};
  bool initialised_ {};
};

} // namespace das::sync
