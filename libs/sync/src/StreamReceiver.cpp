// SPDX-License-Identifier: MIT
#include <das/sync/StreamReceiver.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace das::sync {
namespace {

std::int64_t initialDelay(const std::int64_t producerLeadNs) noexcept {
  constexpr std::int64_t millisecond = 1'000'000;
  // The whole output block must already be written when it is due: the
  // producer's worst-case lead, one output block, and a safety margin.
  const auto wanted = std::max<std::int64_t>(producerLeadNs, 0) + StreamReceiver::kBlockNs +
                      StreamReceiver::kDelayMarginNs;
  const auto rounded = (wanted + millisecond - 1) / millisecond * millisecond;
  return std::clamp(rounded, StreamReceiver::kMinimumDelayNs, StreamReceiver::kMaximumDelayNs);
}

} // namespace

StreamReceiver::StreamReceiver(transport::RingBuffer& ring)
    : ring_(ring),
      scratch_(static_cast<std::size_t>(kBlockFrames * 2 + SincInterpolator::kTaps * 2) * 2) {}

std::int64_t StreamReceiver::appliedOffsetNs() const noexcept {
  return std::min((autoCompensation_ ? delayNs_ : 0) + manualAppliedNs_, kMaximumAppliedOffsetNs);
}

std::int64_t StreamReceiver::nextWakeNs(const std::int64_t nowNs) const noexcept {
  if (state_ == State::idle) return nowNs + 5'000'000;
  return std::max(nowNs, startNs_ + static_cast<std::int64_t>(blockIndex_) * kBlockNs);
}

void StreamReceiver::start(const std::int64_t nowNs, const transport::ClockSnapshot& clock,
                           const transport::ProducerControl& control) {
  if (hasEmitted_) ++resyncs_;
  epoch_ = clock.epoch;
  delayNs_ = initialDelay(control.leadNs);
  autoCompensation_ = control.autoCompensation;
  manualAppliedNs_ = manualSeenNs_;
  startNs_ = nowNs;
  blockIndex_ = 0;
  position_ = clock.frameAt(nowNs - delayNs_);
  needsTimestampBreak_ = true;
  graceUntilNs_ = nowNs + kStartGraceNs;
  state_ = State::running;
}

void StreamReceiver::skipBlock(const std::int64_t dueNs,
                               const transport::ClockSnapshot& clock) noexcept {
  ++blockIndex_;
  position_ = clock.frameAt(dueNs + kBlockNs - delayNs_);
  needsTimestampBreak_ = true;
}

void StreamReceiver::resync(const std::int64_t dueNs, const transport::ClockSnapshot& clock) {
  position_ = clock.frameAt(dueNs - delayNs_);
  needsTimestampBreak_ = true;
  ++resyncs_;
}

void StreamReceiver::publishStatus() noexcept {
  ring_.setConsumerStatus({state_ == State::running ? delayNs_ : 0, appliedOffsetNs(),
                           resyncs_, underruns_});
}

bool StreamReceiver::render(const double step) {
  constexpr auto half = static_cast<std::uint64_t>(SincInterpolator::kHalfTaps);
  const auto first = position_;
  if (!(first > static_cast<double>(half))) return false;
  const auto last = first + step * static_cast<double>(kBlockFrames - 1);
  const auto firstBase = static_cast<std::uint64_t>(std::floor(first));
  const auto lastBase = static_cast<std::uint64_t>(std::floor(last));
  const auto startFrame = firstBase - (half - 1);
  const auto endFrame = lastBase + half + 1;
  const auto frames = endFrame - startFrame;
  if (frames * 2 > scratch_.size()) return false;
  if (!ring_.readAt(startFrame, static_cast<std::uint32_t>(frames), scratch_)) return false;

  for (std::uint32_t frame = 0; frame < kBlockFrames; ++frame) {
    const auto position = first + step * static_cast<double>(frame);
    const auto base = static_cast<std::uint64_t>(std::floor(position));
    const auto fraction = position - static_cast<double>(base);
    const auto local = static_cast<std::size_t>(base - startFrame);
    rendered_[frame * 2] = interpolator_.at(scratch_.data(), local, 2, fraction);
    rendered_[frame * 2 + 1] = interpolator_.at(scratch_.data() + 1, local, 2, fraction);
  }
  return true;
}

bool StreamReceiver::poll(const std::int64_t nowNs, Block& block) {
  ring_.notifyConsumer();
  if (!ring_.isCompatible()) {
    state_ = State::idle;
    haveLastClock_ = false;
    return false;
  }

  const auto write = ring_.writePosition();
  if (write != lastWritePosition_) {
    lastWritePosition_ = write;
    lastWriteChangeNs_ = nowNs;
  }
  const auto producerAlive =
      lastWriteChangeNs_ != 0 && nowNs - lastWriteChangeNs_ < kProducerTimeoutNs;
  transport::ClockSnapshot clock;
  auto hasClock = ring_.readClock(clock);
  if (hasClock) {
    lastClock_ = clock;
    haveLastClock_ = true;
  } else if (haveLastClock_ && producerAlive) {
    // The read lost the race with a writer mid-update. The model barely moves
    // between updates, so the last good snapshot is safe for this poll; a
    // missing producer is detected by producerAlive, not by this.
    clock = lastClock_;
    hasClock = true;
  }
  const auto control = ring_.producerControl();

  const auto manual = std::clamp(control.manualOffsetNs, -kMaximumOffsetNs, kMaximumOffsetNs);
  if (manual != manualSeenNs_) {
    manualSeenNs_ = manual;
    manualChangedAtNs_ = nowNs;
  }

  if (state_ == State::idle) {
    if (!hasClock || !producerAlive) {
      publishStatus();
      return false;
    }
    start(nowNs, clock, control);
  }

  auto dueNs = startNs_ + static_cast<std::int64_t>(blockIndex_) * kBlockNs;
  if (nowNs < dueNs) return false;

  if (!hasClock || !producerAlive) {
    // The DAW stopped processing. Wait for it instead of growing the delay.
    state_ = State::idle;
    publishStatus();
    return false;
  }

  if (nowNs - dueNs > kStallResyncNs) {
    // This thread was starved; restart the block timeline at the present.
    startNs_ = nowNs;
    blockIndex_ = 0;
    dueNs = nowNs;
    resync(dueNs, clock);
  }

  if (clock.epoch != epoch_) {
    // The producer re-anchored (re-prepared, paused or rendered offline).
    epoch_ = clock.epoch;
    delayNs_ = initialDelay(control.leadNs);
    graceUntilNs_ = nowNs + kStartGraceNs;
    resync(dueNs, clock);
  }

  if (control.autoCompensation != autoCompensation_) {
    autoCompensation_ = control.autoCompensation;
    needsTimestampBreak_ = true;
  }
  if (manualSeenNs_ != manualAppliedNs_ && nowNs - manualChangedAtNs_ >= kOffsetSettleNs) {
    manualAppliedNs_ = manualSeenNs_;
    needsTimestampBreak_ = true;
  }

  // Follow the producer's clock: read the frame that was processed delayNs_
  // before this block's slot, nudging the speed to remove residual error.
  const auto target = clock.frameAt(dueNs - delayNs_);
  auto error = target - position_;
  if (std::abs(error) > kHardResyncFrames) {
    resync(dueNs, clock);
    error = 0.0;
  }
  const auto nominalStep = (1.0e9 / static_cast<double>(kSampleRate)) / clock.periodNs;
  const auto correction = std::clamp(kPositionGain * error / static_cast<double>(kBlockFrames),
                                     -kMaximumCorrection, kMaximumCorrection);
  const auto step = nominalStep + correction;

  const auto lastNeeded = position_ + step * static_cast<double>(kBlockFrames - 1);
  if (position_ < static_cast<double>(SincInterpolator::kHalfTaps)) {
    // Before the first frame the producer ever wrote.
    skipBlock(dueNs, clock);
    publishStatus();
    return false;
  }
  const auto endNeeded = static_cast<std::uint64_t>(std::floor(lastNeeded)) +
                         static_cast<std::uint64_t>(SincInterpolator::kHalfTaps) + 1;
  if (endNeeded > write) {
    // Only a producer that wrote recently is "behind"; one that stopped
    // writing (engine stopping, before the timeout above) is not a reason to
    // read further back.
    const auto writingNow = nowNs - lastWriteChangeNs_ <= control.leadNs + kFreshWriteMarginNs;
    if (nowNs >= graceUntilNs_ && writingNow) {
      // The producer is alive but behind our read point: read further back.
      ++underruns_;
      ++resyncs_;
      delayNs_ = std::min(delayNs_ + kDelayStepNs, kMaximumDelayNs);
    }
    skipBlock(dueNs, clock);
    publishStatus();
    return false;
  }

  if (!render(step)) {
    ++resyncs_;
    skipBlock(dueNs, clock);
    publishStatus();
    return false;
  }
  position_ += step * static_cast<double>(kBlockFrames);
  ++blockIndex_;
  const auto release = std::floor(position_) - SincInterpolator::kHalfTaps;
  if (release > 0.0) ring_.releaseUpTo(static_cast<std::uint64_t>(release));

  const auto timestampNs = dueNs - appliedOffsetNs();
  if (needsTimestampBreak_) {
    batching_ = false; // a new break restarts any batch in progress
    if (hasEmitted_) {
      // Hold back output until OBS can no longer smooth the new timestamps
      // onto the previous timeline; the audio in between is dropped as silence.
      const auto expected = static_cast<std::int64_t>(lastTimestampNs_) + kBlockNs;
      if (std::llabs(timestampNs - expected) < kTimestampBreakNs) {
        publishStatus();
        return false;
      }
    }
    needsTimestampBreak_ = false;
    batching_ = true;
    batchFrames_ = 0;
    batchTimestampNs_ = static_cast<std::uint64_t>(timestampNs);
  }

  if (batching_) {
    std::copy(rendered_.begin(), rendered_.end(), batch_.begin() + batchFrames_ * 2);
    batchFrames_ += kBlockFrames;
    if (batchFrames_ < kMaximumPacketFrames) {
      publishStatus();
      return false;
    }
    std::copy(batch_.begin(), batch_.end(), block.samples.begin());
    block.frames = batchFrames_;
    block.timestampNs = batchTimestampNs_;
    batching_ = false;
  } else {
    std::copy(rendered_.begin(), rendered_.end(), block.samples.begin());
    block.frames = kBlockFrames;
    block.timestampNs = static_cast<std::uint64_t>(timestampNs);
  }
  hasEmitted_ = true;
  // The newest block's start, so the next block is expected kBlockNs later.
  lastTimestampNs_ = static_cast<std::uint64_t>(timestampNs);
  publishStatus();
  return true;
}

} // namespace das::sync
