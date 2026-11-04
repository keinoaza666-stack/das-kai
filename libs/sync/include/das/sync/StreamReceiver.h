// SPDX-License-Identifier: MIT
#pragma once

#include <das/sync/SincInterpolator.h>
#include <das/transport/RingBuffer.h>

#include <array>
#include <cstdint>
#include <vector>

namespace das::sync {

// Consumer side of the transport, independent of OBS so it can be tested with
// simulated time. It emits fixed 10 ms blocks on the system clock and reads
// the ring with an adaptive fractional position that follows the producer's
// clock model, which absorbs the drift between the DAW's audio clock and the
// system clock without ever discarding or repeating audio.
//
// Timestamps carry the time each block was processed in the DAW (minus the
// user's manual offset). OBS keeps timestamps within 70 ms of its expectation
// on a continuous timeline, so whenever the offset changes the receiver
// leaves a gap long enough for OBS to accept the new placement.
class StreamReceiver final {
public:
  static constexpr std::uint32_t kSampleRate = transport::kAudioSampleRate;
  static constexpr std::uint32_t kBlockFrames = 480;
  static constexpr std::int64_t kBlockNs = 10'000'000;
  static constexpr std::int64_t kMinimumDelayNs = 15'000'000;
  static constexpr std::int64_t kDelayMarginNs = 8'000'000;
  static constexpr std::int64_t kDelayStepNs = 10'000'000;
  static constexpr std::int64_t kMaximumDelayNs = 400'000'000;
  // OBS smooths timestamp differences below 70 ms (TS_SMOOTHING_THRESHOLD).
  static constexpr std::int64_t kTimestampBreakNs = 80'000'000;
  static constexpr std::int64_t kProducerTimeoutNs = 200'000'000;
  // A producer counts as actively writing if its last write is at most its
  // reported lead plus this margin ago.
  static constexpr std::int64_t kFreshWriteMarginNs = 20'000'000;
  static constexpr std::int64_t kStallResyncNs = 250'000'000;
  static constexpr std::int64_t kOffsetSettleNs = 300'000'000;
  // After (re)starting, missing data is expected while the producer's
  // converter fills up; it is skipped without growing the delay.
  static constexpr std::int64_t kStartGraceNs = 300'000'000;
  static constexpr std::int64_t kMaximumOffsetNs = 1'200'000'000;
  // Ceiling for the total shift (auto + manual) placed earlier in OBS. OBS
  // buffers at most 960 ms (45 ticks of 1024 frames at 48 kHz); keep room
  // for the receiver's own block and other sources.
  static constexpr std::int64_t kMaximumAppliedOffsetNs = 850'000'000;
  // Proportional correction of the read position, per block, and its limit.
  static constexpr double kPositionGain = 0.005;
  static constexpr double kMaximumCorrection = 300.0e-6;
  static constexpr double kHardResyncFrames = 240.0;
  // After every timestamp break the first output is sent as one packet of
  // this many blocks. OBS ignores a source holding less than one mixing tick
  // (1024 frames) when it decides to add buffering, and then trims the late
  // start of the audio instead, shifting it by up to a tick.
  static constexpr std::uint32_t kBreakBatchBlocks = 3;
  static constexpr std::uint32_t kMaximumPacketFrames = kBlockFrames * kBreakBatchBlocks;

  enum class State : std::uint8_t { idle, running };

  struct Block final {
    std::array<float, kMaximumPacketFrames * 2> samples {}; // interleaved stereo
    std::uint32_t frames {};       // kBlockFrames, or a batch after a break
    std::uint64_t timestampNs {};  // time of the first frame
  };

  explicit StreamReceiver(transport::RingBuffer& ring);

  // Emits at most one block per call once the next block is due. Call at
  // least every few milliseconds; nextWakeNs() says when to call again.
  bool poll(std::int64_t nowNs, Block& block);
  [[nodiscard]] std::int64_t nextWakeNs(std::int64_t nowNs) const noexcept;

  [[nodiscard]] State state() const noexcept { return state_; }
  [[nodiscard]] std::int64_t delayNs() const noexcept { return delayNs_; }
  [[nodiscard]] std::int64_t appliedOffsetNs() const noexcept;
  [[nodiscard]] std::uint64_t resyncs() const noexcept { return resyncs_; }
  [[nodiscard]] std::uint64_t underruns() const noexcept { return underruns_; }
  [[nodiscard]] double readPosition() const noexcept { return position_; }

private:
  void start(std::int64_t nowNs, const transport::ClockSnapshot& clock,
             const transport::ProducerControl& control);
  void resync(std::int64_t dueNs, const transport::ClockSnapshot& clock);
  // Drops the block due at dueNs and aims the read position at the next one.
  void skipBlock(std::int64_t dueNs, const transport::ClockSnapshot& clock) noexcept;
  void publishStatus() noexcept;
  bool render(double step);

  transport::RingBuffer& ring_;
  SincInterpolator interpolator_;
  std::vector<float> scratch_;
  std::array<float, kBlockFrames * 2> rendered_ {};
  std::array<float, kMaximumPacketFrames * 2> batch_ {};
  std::uint32_t batchFrames_ {};
  std::uint64_t batchTimestampNs_ {};
  bool batching_ {};
  State state_ {State::idle};
  std::uint64_t epoch_ {};
  std::int64_t startNs_ {};
  std::uint64_t blockIndex_ {};
  double position_ {};
  std::int64_t delayNs_ {};
  std::int64_t graceUntilNs_ {};
  transport::ClockSnapshot lastClock_ {};
  bool haveLastClock_ {};
  bool autoCompensation_ {true};
  std::int64_t manualSeenNs_ {};
  std::int64_t manualAppliedNs_ {};
  std::int64_t manualChangedAtNs_ {};
  bool needsTimestampBreak_ {};
  bool hasEmitted_ {};
  std::uint64_t lastTimestampNs_ {};
  std::uint64_t lastWritePosition_ {};
  std::int64_t lastWriteChangeNs_ {};
  std::uint64_t resyncs_ {};
  std::uint64_t underruns_ {};
};

} // namespace das::sync
