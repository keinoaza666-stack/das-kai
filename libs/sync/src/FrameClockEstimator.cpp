// SPDX-License-Identifier: MIT
#include <das/sync/FrameClockEstimator.h>

#include <algorithm>
#include <cmath>

namespace das::sync {

void FrameClockEstimator::reset(const double nominalPeriodNs) noexcept {
  nominalPeriodNs_ = nominalPeriodNs > 0.0 ? nominalPeriodNs : 1.0e9 / 48000.0;
  slope_ = 0.0;
  lastGoodSlope_ = 0.0;
  hostSpanNs_ = 0.0;
  callGapNs_ = 0.0;
  spreadNs_ = 0.0;
  distanceCount_ = 0;
  distanceNext_ = 0;
  lastStepNs_ = 0.0;
  initialised_ = false;
  anchor(0.0, 0);
  initialised_ = false;
}

void FrameClockEstimator::anchor(const double frame, const std::int64_t timeNs) noexcept {
  // The learned slope and spread survive a re-anchor: the hardware clock and
  // the host's habits did not change just because the host paused.
  referenceFrame_ = frame;
  referenceTimeNs_ = timeNs;
  anchorFrame_ = frame;
  slopeOriginFrame_ = frame;
  segmentsSinceAnchor_ = 0;
  lastObservedFrame_ = frame;
  lineFrame_ = frame;
  intercept_ = 0.0;
  first_ = 0;
  count_ = 0;
  segmentCount_ = 0;
  lastObservedTimeNs_ = timeNs;
  excursion_ = false;
  initialised_ = true;
}

FrameClockEstimator::Point& FrameClockEstimator::point(const std::size_t index) noexcept {
  return points_[(first_ + index) % kMaximumPoints];
}

const FrameClockEstimator::Point& FrameClockEstimator::point(const std::size_t index) const noexcept {
  return points_[(first_ + index) % kMaximumPoints];
}

void FrameClockEstimator::updateSpread(const double distanceNs) noexcept {
  distances_[distanceNext_] = distanceNs;
  distanceNext_ = (distanceNext_ + 1) % kSpreadPoints;
  distanceCount_ = std::min(distanceCount_ + 1, kSpreadPoints);
  auto sorted = distances_;
  const auto middle = sorted.begin() + static_cast<std::ptrdiff_t>(distanceCount_ / 2);
  std::nth_element(sorted.begin(), middle, sorted.begin() + static_cast<std::ptrdiff_t>(distanceCount_));
  spreadNs_ = *middle;
}

FrameClockEstimator::Line FrameClockEstimator::fit(const std::size_t skipNewest, const double atFrame,
                                                   const bool fitSlope, const double prior) const noexcept {
  const auto priorError = kSlopePriorPpm * 1.0e-6 * nominalPeriodNs_;
  Line line {atFrame, intercept_ + slope_ * (atFrame - lineFrame_), prior, priorError};
  const auto n = count_ - std::min(skipNewest, count_);
  if (n == 0) return line;
  const auto window = std::clamp(trackedNs(), kMinimumWindowNs, kWindowNs * scale());
  const auto newest = point(n - 1).frame;
  double w {}, su {}, suu {}, sv {}, suv {}, svv {};
  for (std::size_t i = 0; i < n; ++i) {
    const auto& p = point(i);
    const auto weight = std::exp(-(newest - p.frame) * nominalPeriodNs_ / window);
    const auto u = p.frame - atFrame;
    const auto v = p.v - line.intercept; // relative, for precision
    w += weight;
    su += weight * u;
    suu += weight * u * u;
    sv += weight * v;
    suv += weight * u * v;
    svv += weight * v * v;
  }
  const auto sxx = suu - su * su / w;
  line.dataSlope = prior;
  if (fitSlope && n >= 3 && sxx > 0.0) {
    const auto slope = (suv - su * sv / w) / sxx;
    line.dataSlope = slope;
    const auto level = (sv - slope * su) / w;
    // Weighted mean square residual of the data's own line.
    const auto rss = svv - 2.0 * slope * suv - 2.0 * level * sv + slope * slope * suu +
                     2.0 * slope * level * su + level * level * w;
    // n / (n - 2): the line's own two parameters eat into few points.
    const auto points = static_cast<double>(n);
    const auto variance = kRippleInflation * std::max(rss, 0.0) / w / sxx * points / (points - 2.0);
    const auto priorVariance = priorError * priorError;
    if (variance > 0.0) {
      line.slope = (slope / variance + prior / priorVariance) / (1.0 / variance + 1.0 / priorVariance);
      line.slopeError = std::sqrt(1.0 / (1.0 / variance + 1.0 / priorVariance));
    } else {
      line.slope = slope;
      line.slopeError = 0.0;
    }
  }
  line.slope = std::clamp(line.slope, -kMaxDeviation * nominalPeriodNs_, kMaxDeviation * nominalPeriodNs_);
  line.intercept += (sv - line.slope * su) / w;
  return line;
}

FrameClockEstimator::Update FrameClockEstimator::observe(const double frame,
                                                         const std::int64_t timeNs) noexcept {
  if (!std::isfinite(frame)) return Update::ignored;
  if (!initialised_) {
    anchor(frame, timeNs);
    return Update::anchored;
  }

  const auto progress = frame - lastObservedFrame_;
  if (!(progress > 0.0)) {
    // Going backwards means the producer restarted its frame count.
    if (progress < 0.0) {
      anchor(frame, timeNs);
      return Update::anchored;
    }
    return Update::ignored;
  }
  lastObservedFrame_ = frame;
  const auto callGap = static_cast<double>(timeNs - lastObservedTimeNs_);
  lastObservedTimeNs_ = timeNs;
  if (callGap >= 0.0 && callGap < 1.0e9) callGapNs_ = std::max(callGap, callGapNs_ - callGap / 10.0);
  // A call after a pause of at least half a block starts a burst; inside a
  // burst the blocks follow each other almost at once.
  const auto burstStart = callGap >= 0.5 * progress * nominalPeriodNs_;

  const auto v = static_cast<double>(timeNs - referenceTimeNs_) -
                 (frame - referenceFrame_) * nominalPeriodNs_;
  const auto error = v - (intercept_ + slope_ * (frame - lineFrame_));
  if (error > kResetThresholdNs + hostSpanNs_) {
    // Called far later than the line says: the engine paused or restarted.
    anchor(frame, timeNs);
    return Update::anchored;
  }
  if (error < -kResetThresholdNs) {
    // Called far earlier: offline rendering, or a large chunk processed ahead
    // of the hardware. Only the first one lasts.
    if (!excursion_) {
      excursion_ = true;
      excursionStartNs_ = timeNs;
    } else if (static_cast<double>(timeNs - excursionStartNs_) >= kResetPersistNs) {
      anchor(frame, timeNs);
      return Update::anchored;
    }
    return Update::ignored;
  }
  excursion_ = false;

  if (segmentCount_ == 0) {
    segmentFirstFrame_ = frame;
    segmentStarts_ = 0;
    segmentHighV_ = {v, v};
    segmentLowestV_ = v;
    segmentLowestFrame_ = frame;
  } else {
    if (v > segmentHighV_[0]) {
      segmentHighV_[1] = segmentHighV_[0];
      segmentHighV_[0] = v;
    } else if (segmentCount_ == 1 || v > segmentHighV_[1]) {
      segmentHighV_[1] = v;
    }
    if (v < segmentLowestV_) {
      segmentLowestV_ = v;
      segmentLowestFrame_ = frame;
    }
  }
  if (burstStart) {
    if (segmentStarts_ == kSegmentStarts) {
      // Tiny buffers: keep every other one; the median does not care.
      for (std::size_t i = 0; i < kSegmentStarts / 2; ++i) {
        segmentStartV_[i] = segmentStartV_[2 * i];
        segmentStartFrame_[i] = segmentStartFrame_[2 * i];
      }
      segmentStarts_ = kSegmentStarts / 2;
    }
    segmentStartV_[segmentStarts_] = v;
    segmentStartFrame_[segmentStarts_] = frame;
    ++segmentStarts_;
  }
  ++segmentCount_;
  if ((frame - segmentFirstFrame_) * nominalPeriodNs_ < std::max(kSegmentNs, 3.0 * callGapNs_))
    return Update::ignored;

  // The median burst start, at the mean frame of the burst starts (with the
  // slope near nominal, v barely depends on where in the segment it is).
  // Without burst starts (cannot happen with segments of three call gaps),
  // fall back to the earliest call.
  double pointFrame = segmentLowestFrame_;
  double pointV = segmentLowestV_;
  if (segmentStarts_ > 0) {
    auto sorted = segmentStartV_;
    const auto end = sorted.begin() + static_cast<std::ptrdiff_t>(segmentStarts_);
    const auto middle = sorted.begin() + static_cast<std::ptrdiff_t>(segmentStarts_ / 2);
    std::nth_element(sorted.begin(), middle, end);
    pointV = *middle;
    double frames {};
    for (std::size_t i = 0; i < segmentStarts_; ++i) frames += segmentStartFrame_[i];
    pointFrame = frames / static_cast<double>(segmentStarts_);
  }
  // The span (second latest call to the earliest) sets how late a call may
  // be before it counts as a pause.
  hostSpanNs_ = std::max(segmentHighV_[1] - segmentLowestV_, 0.99 * hostSpanNs_);
  segmentCount_ = 0;
  // The first segment after a (re)start holds the start-up burst.
  if (++segmentsSinceAnchor_ == 1) return Update::ignored;
  return addPoint(pointFrame, pointV);
}

FrameClockEstimator::Update FrameClockEstimator::addPoint(const double frame, const double v) noexcept {
  // The first point's distance is from the anchor (a single call), not a line.
  if (count_ > 0) updateSpread(std::abs(v - (intercept_ + slope_ * (frame - lineFrame_))));

  if (count_ == kMaximumPoints) {
    first_ = (first_ + 1) % kMaximumPoints;
    --count_;
  }
  point(count_) = {frame, v};
  ++count_;
  // Drop points whose weight no longer matters.
  const auto window = std::clamp(trackedNs(), kMinimumWindowNs, kWindowNs * scale());
  while (count_ > 1 && (frame - point(0).frame) * nominalPeriodNs_ > 8.0 * window) {
    first_ = (first_ + 1) % kMaximumPoints;
    --count_;
  }

  // Until the points span half a second the slope is not trustworthy; keep
  // the previous estimate and fit only the level.
  const auto fitSlope = slopeSpanNs() >= 0.5e9;
  auto result = Update::tracked;
  constexpr auto stepPoints = static_cast<std::size_t>(kStepPoints);
  const auto settled = count_ >= stepPoints + 3;
  if (settled) {
    // Compare the newest points with the line through the older ones.
    const auto reference = fit(stepPoints, frame, fitSlope, lastGoodSlope_);
    const auto older = count_ - stepPoints;
    const auto measured = std::min(older, kStepRangePoints);
    double lowest {};
    double highest {};
    for (std::size_t i = older - measured; i < older; ++i) {
      const auto& p = point(i);
      const auto residual = p.v - (reference.intercept + reference.slope * (p.frame - frame));
      lowest = i == older - measured ? residual : std::min(lowest, residual);
      highest = i == older - measured ? residual : std::max(highest, residual);
    }
    auto threshold = std::max(kStepFloorNs, kStepRangeFactor * (highest - lowest));
    if (measured < kStepEarlyPoints) threshold = std::max(threshold, kStepEarlyFloorNs);
    std::array<double, stepPoints> residuals {};
    bool step = true;
    for (std::size_t j = 0; j < stepPoints; ++j) {
      const auto& p = point(count_ - stepPoints + j);
      residuals[j] = p.v - (reference.intercept + reference.slope * (p.frame - frame));
      step = step && std::abs(residuals[j]) > threshold && (residuals[j] > 0.0) == (residuals[0] > 0.0);
    }
    if (step) {
      auto sorted = residuals;
      std::sort(sorted.begin(), sorted.end());
      const auto size = (sorted[stepPoints / 2 - 1] + sorted[stepPoints / 2]) / 2.0;
      // Bring the older points to the new level; they all move by the same
      // amount, so the slope they define does not change. Drop the newest:
      // one of them straddles the step, and if the host is still moving they
      // form a ramp; either would pull the slope.
      for (std::size_t i = 0; i < older; ++i) point(i).v += size;
      count_ = older;
      lastStepNs_ = size;
      result = Update::shifted;
    }
  }

  // The slope from the settled (older) points, the level from all of them.
  const auto sloped = fit(settled ? stepPoints : 0, frame, fitSlope, lastGoodSlope_);
  const auto line = fit(0, frame, false, sloped.slope);
  slopeErrorNs_ = sloped.slopeError;
  if (fitSlope && count_ >= kRampPoints &&
      std::abs(sloped.dataSlope) >= 0.99 * kMaxDeviation * nominalPeriodNs_) {
    // No clock is this far off: the points hold a host ramp. Drop all but the
    // newest few (they keep the level), keep the last good slope, and learn
    // the slope again from the points that follow. Judged only on enough
    // points: three points 0.75 s apart reach the limit with 0.75 ms of
    // jitter, and dropping them again and again never let the slope settle.
    first_ = (first_ + count_ - kStepPoints) % kMaximumPoints;
    count_ = kStepPoints;
    slope_ = lastGoodSlope_;
    slopeErrorNs_ = kSlopePriorPpm * 1.0e-6 * nominalPeriodNs_;
    slopeOriginFrame_ = frame;
  } else {
    slope_ = line.slope;
    if (isLocked()) lastGoodSlope_ = slope_;
  }
  intercept_ = line.intercept;
  lineFrame_ = frame;
  return result;
}

std::int64_t FrameClockEstimator::anchorTimeNs() const noexcept {
  return referenceTimeNs_ +
         static_cast<std::int64_t>(std::llround((lineFrame_ - referenceFrame_) * nominalPeriodNs_ + intercept_));
}

double FrameClockEstimator::driftPpm() const noexcept {
  // A shorter period per frame means the DAW produces frames faster.
  return (nominalPeriodNs_ / periodNs() - 1.0) * 1.0e6;
}

} // namespace das::sync
