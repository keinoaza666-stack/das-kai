// SPDX-License-Identifier: MIT
#include <das/sync/FrameClockEstimator.h>

#include <algorithm>
#include <cmath>

namespace das::sync {

void FrameClockEstimator::reset(const double nominalPeriodNs) noexcept {
  nominalPeriodNs_ = nominalPeriodNs > 0.0 ? nominalPeriodNs : 1.0e9 / 48000.0;
  slope_ = 0.0;
  jitterNs_ = 2.0e6;
  initialised_ = false;
  anchor(0.0, 0);
  initialised_ = false;
}

void FrameClockEstimator::anchor(const double frame, const std::int64_t timeNs) noexcept {
  // The learned slope survives a re-anchor: the hardware clock did not change
  // just because the host paused, so relocking starts from the last estimate.
  referenceFrame_ = frame;
  referenceTimeNs_ = timeNs;
  lastFrame_ = frame;
  weight_ = 1.0;
  sumU_ = 0.0;
  sumUU_ = 0.0;
  sumV_ = 0.0;
  sumUV_ = 0.0;
  intercept_ = 0.0;
  trackedForNs_ = 0.0;
  outliers_ = 0;
  initialised_ = true;
}

void FrameClockEstimator::fit() noexcept {
  const auto determinant = weight_ * sumUU_ - sumU_ * sumU_;
  // Until the window spans enough frames the slope is not trustworthy; keep
  // the previous estimate and fit only the offset.
  if (trackedForNs_ >= 0.5e9 && determinant > 1.0e-9 * weight_ * sumUU_) {
    const auto slope = (weight_ * sumUV_ - sumU_ * sumV_) / determinant;
    slope_ = std::clamp(slope, -kMaxDeviation * nominalPeriodNs_, kMaxDeviation * nominalPeriodNs_);
  }
  intercept_ = (sumV_ - slope_ * sumU_) / weight_;
}

FrameClockEstimator::Update FrameClockEstimator::observe(const double frame,
                                                         const std::int64_t timeNs) noexcept {
  if (!std::isfinite(frame)) return Update::ignored;
  if (!initialised_) {
    anchor(frame, timeNs);
    return Update::anchored;
  }

  const auto frames = frame - lastFrame_;
  if (!(frames > 0.0)) {
    // Going backwards means the producer restarted its frame count.
    if (frames < 0.0) {
      anchor(frame, timeNs);
      return Update::anchored;
    }
    return Update::ignored;
  }

  const auto period = nominalPeriodNs_ + slope_;
  const auto v = static_cast<double>(timeNs - referenceTimeNs_) -
                 (frame - referenceFrame_) * nominalPeriodNs_;
  const auto predicted = intercept_ + slope_ * frames;
  const auto error = v - predicted;
  if (std::abs(error) > kResetThresholdNs ||
      (!isLocked() && std::abs(error) > kLockingResetThresholdNs)) {
    anchor(frame, timeNs);
    return Update::anchored;
  }
  if (isLocked() &&
      std::abs(error) > std::max(kOutlierFloorNs, kOutlierJitterFactor * jitterNs_)) {
    // A late or early burst (host prefetch, a missed cycle). Leave the model
    // alone; if the offset persists the stream really jumped.
    if (++outliers_ >= kOutliersBeforeReanchor) {
      anchor(frame, timeNs);
      return Update::anchored;
    }
    return Update::ignored;
  }
  outliers_ = 0;
  jitterNs_ = 0.98 * jitterNs_ + 0.02 * std::abs(error);

  // Move the origin of u to the new frame, forget old observations, add the new one.
  const auto du = frames;
  sumUU_ += -2.0 * du * sumU_ + du * du * weight_;
  sumUV_ -= du * sumV_;
  sumU_ -= du * weight_;
  const auto stepNs = frames * period;
  trackedForNs_ += stepNs;
  const auto window = std::clamp(trackedForNs_, kMinimumWindowNs, kWindowNs);
  const auto keep = std::exp(-stepNs / window);
  weight_ = weight_ * keep + 1.0;
  sumU_ *= keep;
  sumUU_ *= keep;
  sumV_ = sumV_ * keep + v;
  sumUV_ *= keep;
  lastFrame_ = frame;
  fit();

  // Keep the reference near the present so v stays small.
  if (frame - referenceFrame_ > 1.0e7) {
    const auto shiftFrames = frame - referenceFrame_;
    const auto shiftNs = static_cast<std::int64_t>(std::llround(shiftFrames * nominalPeriodNs_));
    // v is relative to the nominal line; moving the reference along that line
    // changes v by the rounding of shiftNs only.
    const auto rounding = static_cast<double>(shiftNs) - shiftFrames * nominalPeriodNs_;
    referenceFrame_ = frame;
    referenceTimeNs_ += shiftNs;
    sumV_ -= rounding * weight_;
    sumUV_ -= rounding * sumU_;
    intercept_ -= rounding;
  }
  return Update::tracked;
}

std::int64_t FrameClockEstimator::anchorTimeNs() const noexcept {
  return referenceTimeNs_ +
         static_cast<std::int64_t>(std::llround((lastFrame_ - referenceFrame_) * nominalPeriodNs_ +
                                                intercept_));
}

double FrameClockEstimator::driftPpm() const noexcept {
  // A shorter period per frame means the DAW produces frames faster.
  return (nominalPeriodNs_ / periodNs() - 1.0) * 1.0e6;
}

} // namespace das::sync
