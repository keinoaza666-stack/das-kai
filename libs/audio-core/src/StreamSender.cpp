// SPDX-License-Identifier: GPL-3.0-or-later
// Added for DAS Kai (2026-11-04). See MODIFICATIONS.md.
#include <das/audio/StreamSender.h>

#include <algorithm>
#include <cmath>

namespace das::audio {
namespace {
constexpr auto kOutputRate = static_cast<double>(transport::kAudioSampleRate);

std::int64_t secondsToNs(const double seconds) noexcept {
  return static_cast<std::int64_t>(std::llround(seconds * 1.0e9));
}
} // namespace

bool StreamSender::prepare(const double sampleRate, const std::size_t maximumBlockFrames,
                           transport::RingBuffer* const ring) {
  prepared_ = false;
  ring_ = nullptr;
  if (!(sampleRate >= 8000.0 && sampleRate <= 384000.0) || maximumBlockFrames == 0) return false;
  if (!converter_.prepare(sampleRate, kOutputRate, maximumBlockFrames)) return false;
  for (auto& buffer : scratch_) buffer.assign(maximumBlockFrames, 0.0F);
  interleaved_.assign(converter_.maximumOutputFrames() * 2, 0.0F);
  inputRate_ = sampleRate;
  converterLatencyNs_ = secondsToNs(converter_.latencySeconds());
  leadNs_ = secondsToNs(static_cast<double>(maximumBlockFrames) / inputRate_) + converterLatencyNs_;
  clock_.reset(1.0e9 / kOutputRate);
  needsReanchor_ = true;
  if (ring != nullptr && ring->isCompatible()) {
    ring_ = ring;
    // Continue the epoch sequence of whoever produced before, so a reader
    // still attached from a previous session notices the restart.
    epoch_ = ring_->header()->clockEpoch.load(std::memory_order_relaxed);
    ringBaseFrame_ = ring_->writePosition();
    inputFrames_ = 0;
    publishControl();
  }
  prepared_ = true;
  return true;
}

void StreamSender::setControl(const bool autoCompensation,
                              const std::int64_t manualOffsetNs) noexcept {
  autoCompensation_ = autoCompensation;
  manualOffsetNs_ = manualOffsetNs;
}

void StreamSender::publishControl() noexcept {
  if (ring_ != nullptr) ring_->setProducerControl({autoCompensation_, manualOffsetNs_, leadNs_});
}

void StreamSender::process(const float* const left, const float* const right,
                           const std::size_t frames, const std::int64_t nowNs) noexcept {
  if (!prepared_ || ring_ == nullptr || frames == 0) return;

  leadNs_ = std::max(leadNs_, secondsToNs(static_cast<double>(frames) / inputRate_) +
                                  converterLatencyNs_);
  publishControl();

  std::size_t offset {};
  while (offset < frames) {
    const auto chunk = std::min(frames - offset, converter_.maximumInputFrames());
    if (left != nullptr) {
      const auto* const r = right != nullptr ? right : left;
      std::copy_n(left + offset, chunk, scratch_[0].begin());
      std::copy_n(r + offset, chunk, scratch_[1].begin());
    } else {
      std::fill_n(scratch_[0].begin(), chunk, 0.0F);
      std::fill_n(scratch_[1].begin(), chunk, 0.0F);
    }
    const auto produced = converter_.process(std::span<const float>(scratch_[0].data(), chunk),
                                             std::span<const float>(scratch_[1].data(), chunk),
                                             interleaved_);
    if (produced > 0) {
      ring_->notifyProducer();
      ring_->write(std::span<const float>(interleaved_.data(), produced * 2), produced);
    }
    offset += chunk;
  }

  // The last input sample of this block was captured just before the host
  // called us: map its position in 48 kHz ring frames to that moment. The
  // converter delays its output, but frame positions refer to the input
  // timeline, so the model stays exact and the delay shows up in leadNs_.
  inputFrames_ += frames;
  const auto frame = static_cast<double>(ringBaseFrame_) +
                     static_cast<double>(inputFrames_) * (kOutputRate / inputRate_);
  if (needsReanchor_) {
    clock_.reset(clock_.nominalPeriodNs());
    needsReanchor_ = false;
  }
  const auto update = clock_.observe(frame, nowNs);
  if (update == sync::FrameClockEstimator::Update::anchored) ++epoch_;
  if (update != sync::FrameClockEstimator::Update::ignored)
    ring_->publishClock({epoch_, clock_.anchorFrame(), clock_.anchorTimeNs(), clock_.periodNs()});
}

} // namespace das::audio
