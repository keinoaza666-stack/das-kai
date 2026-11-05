// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace das::sync {

// Turns jittery (frame, time) observations from the audio thread into a
// smooth linear clock model:
//
//   time(frame) = anchorTimeNs + (frame - anchorFrame) * periodNs
//
// The producer feeds one observation per processed block: the frame position
// at the end of the block (in 48 kHz ring frames, possibly fractional) and the
// monotonic time at which the block was handed to the plug-in.
//
// Each segment of at least kSegmentNs of audio (and at least three times the
// longest recent gap between calls, so it holds two whole chunks or more)
// gives one point. Only calls that start a burst count (the gap before them
// is at least half a block; in a real-time host that is every call), and of
// those the median relative to its frame: a few odd calls (an early one, and
// the block after it, which then also looks like a burst start; or a late
// one) do not move it, and jitter spreading both ways does not bias it. The model is an exponentially weighted
// least-squares line through the stored points: the slope is the DAW clock's
// real speed, the line at the newest point is when the host starts work on a
// frame that is needed now.
//
// Why burst starts, and not the mean or the earliest call: a host that
// processes several blocks back to back (ASIO-Guard) calls the later ones
// early by a growing amount, a sawtooth. A segment's mean then depends on
// where the chunks fall in it and beats against the segment length; over
// seconds that looks like a clean slope plus jumps (8 x 512 frames: +1400 ppm,
// then -3 ms every 2.1 s). The earliest call is the last block of the largest
// chunk, so it moves when chunk sizes vary (irregular chunks: 15-28 ppm off).
// The first block of a burst is the one the hardware needs next, called when
// it needs it whatever the chunk size.
//
// What the line must survive (Cubase, 2026-11-04, and the reviews after it;
// the sync tests' "host:" cases):
// - Chunked processing, steady or with irregular chunk sizes (the latter
//   reproduces the Cubase symptom with the first version: +2004 ppm and a
//   re-anchor every 0.1 s).
// - Steps: the host starts calling a constant amount earlier or later and
//   stays there (pre-fill, re-render, switching paths). Fitted as data, a step
//   reads as drift. A step is declared only when all of the newest
//   kStepPoints points lie on one side of the line fitted to the older ones,
//   farther than kStepRangeFactor times the whole range the older points
//   spread over around that line (the ripple's full height, not its typical
//   size: a sawtooth's jumps are as tall as its range; an earlier version used
//   the median and took every jump for a step). The older points then move by
//   the step and the newest are dropped (one of them straddles the step and
//   would pull the slope). Points are never rewritten onto a line: such points
//   have no residual and made a wrong slope look certain (second review).
//   Consumers follow the moved line; a step never restarts them.
// - Ramps: a host filling its pre-fetch over seconds moves faster than any
//   clock. When the points' own slope (not the one blended with the prior,
//   which stays far below: a 10 ms/s ramp blended to about 1000 ppm and its
//   points tilted the slope for half a minute after the ramp ended) reaches
//   kMaxDeviation (judged on at least kRampPoints points), all but the newest
//   few points are dropped, the line keeps its level with the last good
//   slope, and the slope is learnt again. The slope that is published always comes from points
//   older than the newest kStepPoints, so a step not yet recognised cannot
//   tilt it; the level comes from all points.
// - A slope fitted to a few seconds of rippling points can be wildly off,
//   and steps are judged by extrapolating it: a wrong early slope made every
//   new point look like a step and the shifts then kept the wrong slope. The
//   fitted slope is therefore weighed against its own standard error (with
//   kRippleInflation for the ripple's correlation) and blended with the last
//   good slope as a prior of kSlopePriorPpm: early it stays near the prior,
//   and it follows the data once the data is clearly better. It is shown as
//   locked only when its error is below kLockedSlopeErrorPpm.
// - The first segment after a (re)start holds the start-up burst; it is
//   dropped.
// - Discontinuities (engine restart, pause, offline render): an observation
//   more than kResetThresholdNs later than the line, beyond the host's own
//   span (the first block of a chunk is called later than the line of
//   earliest calls by the whole chunk), re-anchors at once; one that far
//   early must stay so for kResetPersistNs of real time.
//
// Allocation-free and lock-free so it can run on the real-time audio thread.
class FrameClockEstimator final {
public:
  enum class Update : std::uint8_t {
    anchored, // model (re)started from this observation; consumers must resync
    tracked,  // a point refined the model
    shifted,  // the host's timing stepped: the line moved, the slope is kept
    ignored   // model unchanged (collecting a segment, no progress, excursion)
  };

  static constexpr double kResetThresholdNs = 150.0e6;
  static constexpr double kResetPersistNs = 100.0e6;
  // An audio interface and the system clock differ by tens of ppm, rarely a
  // few hundred; the limit leaves room beyond that (the OBS harness checks
  // 1000 ppm). A slope held at the limit is never shown as locked.
  static constexpr double kMaxDeviation = 1.5e-3;
  static constexpr double kWindowNs = 30.0e9;
  static constexpr double kMinimumWindowNs = 1.0e9;
  // Hosts with a wide spread average longer and lock later: one more
  // kWindowNs (and kLockingDurationNs) per millisecond of spread, up to this.
  static constexpr double kMaximumScale = 4.0;
  static constexpr double kLockingDurationNs = 4.0e9;
  static constexpr double kSegmentNs = 250.0e6;
  static constexpr int kStepPoints = 4;
  static constexpr double kStepFloorNs = 1.0e6;
  static constexpr double kStepRangeFactor = 1.5;
  // The range is measured over up to this many older points; with fewer than
  // kStepEarlyPoints of them it is not known yet and steps must also exceed
  // kStepEarlyFloorNs.
  static constexpr std::size_t kStepRangePoints = 24;
  static constexpr std::size_t kStepEarlyPoints = 8;
  static constexpr double kStepEarlyFloorNs = 3.0e6;
  // Four points a second; with the weights dropped past 8 windows this holds
  // 4 minutes, enough for the normal 30 s window (longer windows truncate).
  static constexpr std::size_t kMaximumPoints = 1024;
  static constexpr double kRippleInflation = 2.0;
  // The ramp check (slope at the limit) needs at least this many points.
  static constexpr std::size_t kRampPoints = 8;
  static constexpr double kSlopePriorPpm = 100.0;
  static constexpr double kLockedSlopeErrorPpm = 20.0;
  static constexpr std::size_t kSpreadPoints = 24;

  void reset(double nominalPeriodNs) noexcept;
  Update observe(double frame, std::int64_t timeNs) noexcept;

  [[nodiscard]] bool hasModel() const noexcept { return initialised_; }
  [[nodiscard]] double anchorFrame() const noexcept { return lineFrame_; }
  [[nodiscard]] std::int64_t anchorTimeNs() const noexcept;
  [[nodiscard]] double periodNs() const noexcept { return nominalPeriodNs_ + slope_; }
  [[nodiscard]] double nominalPeriodNs() const noexcept { return nominalPeriodNs_; }
  // Positive when the DAW clock runs fast relative to the system clock.
  [[nodiscard]] double driftPpm() const noexcept;
  // Settled: tracked long enough (longer for a host with a wide spread), and
  // the slope is not held at the limit.
  [[nodiscard]] bool isLocked() const noexcept {
    return initialised_ && slopeSpanNs() >= kLockingDurationNs * scale() &&
           slopeErrorPpm() < kLockedSlopeErrorPpm &&
           std::abs(slope_) < 0.99 * kMaxDeviation * nominalPeriodNs_;
  }
  // Standard error of the slope, in ppm.
  [[nodiscard]] double slopeErrorPpm() const noexcept { return slopeErrorNs_ / nominalPeriodNs_ * 1.0e6; }
  // The latest step the line moved by (ns, negative = calls came earlier).
  [[nodiscard]] double lastStepNs() const noexcept { return lastStepNs_; }
  // Typical distance of a point from the line (median of recent ones), ns.
  [[nodiscard]] double spreadNs() const noexcept { return spreadNs_; }

private:
  struct Point {
    double frame {};
    double v {}; // time minus the nominal line through the reference, ns
  };
  struct Line {
    double frame {};      // where the line is evaluated
    double intercept {};  // v at that frame
    double slope {};      // ns per frame beyond nominal
    double slopeError {}; // its standard error, ns per frame
    double dataSlope {};  // the points' own slope, before the prior
  };

  void anchor(double frame, std::int64_t timeNs) noexcept;
  Update addPoint(double frame, double v) noexcept;
  // Weighted fit over stored points [0, count - skipNewest), evaluated at
  // atFrame. With fitSlope the slope is the data's, blended with `prior` by
  // their errors; otherwise it is `prior`.
  [[nodiscard]] Line fit(std::size_t skipNewest, double atFrame, bool fitSlope, double prior) const noexcept;
  [[nodiscard]] Point& point(std::size_t index) noexcept; // 0 = oldest
  [[nodiscard]] const Point& point(std::size_t index) const noexcept;
  [[nodiscard]] double scale() const noexcept {
    return std::clamp(spreadNs_ / 1.0e6, 1.0, kMaximumScale);
  }
  [[nodiscard]] double trackedNs() const noexcept {
    return count_ > 0 ? (point(count_ - 1).frame - anchorFrame_) * nominalPeriodNs_ : 0.0;
  }
  // How long the slope has been learnt from (since the anchor or the last
  // time it was reset at the limit).
  [[nodiscard]] double slopeSpanNs() const noexcept {
    return count_ > 0 ? (point(count_ - 1).frame - slopeOriginFrame_) * nominalPeriodNs_ : 0.0;
  }
  void updateSpread(double distanceNs) noexcept;

  double nominalPeriodNs_ {1.0e9 / 48000.0};
  double referenceFrame_ {};
  std::int64_t referenceTimeNs_ {};
  double anchorFrame_ {};
  double slopeOriginFrame_ {};
  double lastGoodSlope_ {};
  int segmentsSinceAnchor_ {};
  double lastObservedFrame_ {};
  // The published line.
  double lineFrame_ {};
  double intercept_ {};
  double slope_ {};
  double slopeErrorNs_ {};
  // Stored points, a ring.
  std::array<Point, kMaximumPoints> points_ {};
  std::size_t first_ {};
  std::size_t count_ {};
  // The segment being collected.
  double segmentFirstFrame_ {};
  // Burst starts of the segment (v and frame; beyond the capacity every
  // other one is kept), the two latest calls and the earliest call.
  static constexpr std::size_t kSegmentStarts = 512;
  std::array<double, kSegmentStarts> segmentStartV_ {};
  std::array<double, kSegmentStarts> segmentStartFrame_ {};
  std::size_t segmentStarts_ {};
  std::array<double, 2> segmentHighV_ {};
  double segmentLowestV_ {};
  double segmentLowestFrame_ {};
  int segmentCount_ {};
  // Longest recent gap between calls (wall time), and the widest spread of
  // calls within a segment (the host's chunk span), both fading.
  std::int64_t lastObservedTimeNs_ {};
  double callGapNs_ {};
  double hostSpanNs_ {};
  // Recent distances from the line, for the spread median.
  std::array<double, kSpreadPoints> distances_ {};
  std::size_t distanceCount_ {};
  std::size_t distanceNext_ {};
  double spreadNs_ {};
  // An early excursion beyond kResetThresholdNs, waiting to persist.
  bool excursion_ {};
  std::int64_t excursionStartNs_ {};
  double lastStepNs_ {};
  bool initialised_ {};
};

} // namespace das::sync
