// SPDX-License-Identifier: GPL-3.0-or-later
// Added for DAS Kai (2026-11-04). See MODIFICATIONS.md.
#pragma once

#include <das/audio/SampleRateConverter.h>
#include <das/sync/FrameClockEstimator.h>
#include <das/transport/RingBuffer.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace das::audio {

// Producer side of the transport: converts DAW audio to 48 kHz, writes it to
// the ring and publishes the clock model and the user's sync controls. Shared
// by the plug-in and the tests so both exercise the same code path.
class StreamSender final {
public:
  // Allocates; call from prepareToPlay, never from the audio thread.
  bool prepare(double sampleRate, std::size_t maximumBlockFrames,
               transport::RingBuffer* ring);
  void detach() noexcept { ring_ = nullptr; }

  // Audio thread. `right` may equal `left` for mono; both null sends silence.
  // `nowNs` is CLOCK_UPTIME_RAW when the host handed over this block.
  void process(const float* left, const float* right, std::size_t frames,
               std::int64_t nowNs) noexcept;
  void setControl(bool autoCompensation, std::int64_t manualOffsetNs) noexcept;
  // Call when blocks were skipped on purpose (offline export) so the clock
  // re-anchors instead of treating the jump as drift.
  void markDiscontinuity() noexcept { needsReanchor_ = true; }

  [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }
  [[nodiscard]] double driftPpm() const noexcept { return clock_.driftPpm(); }
  [[nodiscard]] bool clockLocked() const noexcept { return clock_.isLocked(); }
  [[nodiscard]] std::int64_t leadNs() const noexcept { return leadNs_; }
  [[nodiscard]] std::uint64_t epoch() const noexcept { return epoch_; }

private:
  void publishControl() noexcept;

  SampleRateConverter converter_;
  sync::FrameClockEstimator clock_;
  transport::RingBuffer* ring_ {};
  std::vector<float> interleaved_;
  std::array<std::vector<float>, 2> scratch_;
  double inputRate_ {48000.0};
  std::uint64_t ringBaseFrame_ {};
  std::uint64_t inputFrames_ {};
  std::uint64_t epoch_ {};
  std::int64_t leadNs_ {};
  std::int64_t converterLatencyNs_ {};
  bool autoCompensation_ {true};
  std::int64_t manualOffsetNs_ {};
  bool needsReanchor_ {true};
  bool prepared_ {};
};

} // namespace das::audio
