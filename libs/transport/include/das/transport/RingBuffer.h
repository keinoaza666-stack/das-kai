// SPDX-License-Identifier: MIT
// Modified for DAS Kai (2026-11-04): protocol v2 (own identifiers, always-write ring,
// clock model, sync controls, consumer status). See MODIFICATIONS.md.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace das::transport {

// DAS Kai protocol. Deliberately different from upstream DawAudioStreamer
// ("DSAS" v1) so both products can be installed side by side without either
// one attaching to the other's shared memory.
inline constexpr std::uint32_t kProtocolMagic = 0x4B535452; // "KSTR"
inline constexpr std::uint16_t kProtocolMajor = 2;
inline constexpr std::uint16_t kProtocolMinor = 0;
inline constexpr std::uint32_t kAudioSampleRate = 48000;
inline constexpr std::uint16_t kAudioChannels = 2;
inline constexpr std::uint32_t kAudioCapacityFrames = 96000;
inline constexpr std::wstring_view kObsAudioMappingName = L"Local\\KinuStream.OBS.v2";

// Producer control flags.
inline constexpr std::uint64_t kFlagAutoCompensation = 1ULL << 0;

// A linear model mapping ring frames to the monotonic clock shared by the DAW
// plug-in and OBS (CLOCK_UPTIME_RAW on macOS):
//   timeNs(frame) = anchorTimeNs + (frame - anchorFrame) * periodNs
struct ClockSnapshot final {
  std::uint64_t epoch {};       // changes whenever the producer re-anchors
  double anchorFrame {};
  std::int64_t anchorTimeNs {};
  double periodNs {};

  [[nodiscard]] bool isValid() const noexcept { return periodNs > 0.0; }
  [[nodiscard]] double frameAt(const std::int64_t timeNs) const noexcept {
    return anchorFrame + static_cast<double>(timeNs - anchorTimeNs) / periodNs;
  }
  [[nodiscard]] double timeAt(const double frame) const noexcept {
    return static_cast<double>(anchorTimeNs) + (frame - anchorFrame) * periodNs;
  }
};

struct ProducerControl final {
  bool autoCompensation {true};
  std::int64_t manualOffsetNs {}; // positive = place DAW audio earlier
  std::int64_t leadNs {};         // worst-case delay before a frame is written
};

struct ConsumerStatus final {
  std::int64_t delayNs {};          // how far behind real time the consumer reads
  std::int64_t appliedOffsetNs {};  // total shift currently applied to timestamps
  std::uint64_t resyncs {};
  std::uint64_t underruns {};
};

struct alignas(64) RingHeader final {
  // Cache line 0: layout and SPSC positions.
  std::uint32_t magic {kProtocolMagic};
  std::uint16_t major {kProtocolMajor};
  std::uint16_t minor {kProtocolMinor};
  std::uint32_t capacityFrames {};
  std::uint16_t channels {};
  std::uint16_t reserved {};
  std::uint32_t sampleRate {48000};
  std::uint32_t reserved2 {};
  std::atomic<std::uint64_t> writePosition {};
  std::atomic<std::uint64_t> readPosition {};      // consumer release point
  std::atomic<std::uint64_t> droppedFrames {};     // overwritten before release
  std::atomic<std::uint64_t> producerHeartbeat {};
  std::atomic<std::uint64_t> consumerHeartbeat {};

  // Cache line 1: producer clock model (seqlock) and controls.
  std::atomic<std::uint64_t> clockSequence {};
  std::atomic<std::uint64_t> clockEpoch {};
  std::atomic<std::uint64_t> clockAnchorFrameBits {};
  std::atomic<std::uint64_t> clockAnchorTimeNs {};
  std::atomic<std::uint64_t> clockPeriodBits {};
  std::atomic<std::uint64_t> producerFlags {kFlagAutoCompensation};
  std::atomic<std::uint64_t> manualOffsetNs {};
  std::atomic<std::uint64_t> producerLeadNs {};

  // Cache line 2: consumer feedback shown in the plug-in window.
  std::atomic<std::uint64_t> consumerDelayNs {};
  std::atomic<std::uint64_t> consumerAppliedOffsetNs {};
  std::atomic<std::uint64_t> consumerResyncs {};
  std::atomic<std::uint64_t> consumerUnderruns {};
  std::atomic<std::uint64_t> reserved3[4] {};
};

static_assert(sizeof(RingHeader) == 192);
static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "The transport needs lock-free 64-bit atomics");

[[nodiscard]] std::size_t requiredBytes(std::uint32_t capacityFrames,
                                        std::uint16_t channels) noexcept;

// Single-producer ring in shared memory. The producer always writes (oldest
// audio is overwritten when nobody reads), so the write position doubles as a
// continuous frame clock for the timestamp model. Readers either consume
// sequentially (read) or fetch absolute frame ranges (readAt) and validate
// that the producer did not overwrite them meanwhile.
class RingBuffer final {
public:
  static bool initialize(std::span<std::byte> storage,
                         std::uint32_t capacityFrames,
                         std::uint16_t channels,
                         std::uint32_t sampleRate = 48000) noexcept;

  explicit RingBuffer(std::span<std::byte> storage) noexcept;
  [[nodiscard]] bool isCompatible() const noexcept;
  [[nodiscard]] std::uint32_t capacityFrames() const noexcept;
  [[nodiscard]] std::uint64_t writePosition() const noexcept;
  [[nodiscard]] std::uint32_t availableToRead() const noexcept;
  [[nodiscard]] std::uint32_t availableToWrite() const noexcept;
  // Always stores every frame; returns the number written.
  std::uint32_t write(std::span<const float> interleaved, std::uint32_t frames) noexcept;
  [[nodiscard]] std::uint32_t read(std::span<float> interleaved,
                                   std::uint32_t frames) noexcept;
  // Copies frames [startFrame, startFrame + frames). Returns false when any of
  // them is not written yet or was overwritten before the copy finished.
  [[nodiscard]] bool readAt(std::uint64_t startFrame, std::uint32_t frames,
                            std::span<float> interleaved) const noexcept;
  void releaseUpTo(std::uint64_t frame) noexcept;
  void discardAll() noexcept;
  void notifyProducer() noexcept;
  void notifyConsumer() noexcept;
  [[nodiscard]] std::uint64_t producerHeartbeat() const noexcept;
  [[nodiscard]] std::uint64_t consumerHeartbeat() const noexcept;

  // Clock model: published by the producer only, read by the consumer.
  void publishClock(const ClockSnapshot& snapshot) noexcept;
  [[nodiscard]] bool readClock(ClockSnapshot& snapshot) const noexcept;
  void setProducerControl(const ProducerControl& control) noexcept;
  [[nodiscard]] ProducerControl producerControl() const noexcept;
  void setConsumerStatus(const ConsumerStatus& status) noexcept;
  [[nodiscard]] ConsumerStatus consumerStatus() const noexcept;

  [[nodiscard]] const RingHeader* header() const noexcept { return header_; }

private:
  RingHeader* header_ {};
  float* data_ {};
  std::size_t storageBytes_ {};
};

} // namespace das::transport
