// SPDX-License-Identifier: MIT
// Modified for Kinu Stream (2026-11-04): protocol v2 (own identifiers, always-write ring,
// clock model, sync controls, consumer status). See MODIFICATIONS.md.
#include <das/transport/RingBuffer.h>

#include <algorithm>
#include <bit>
#include <limits>
#include <new>

namespace das::transport {

std::size_t requiredBytes(const std::uint32_t capacityFrames,
                          const std::uint16_t channels) noexcept {
  if (capacityFrames == 0 || channels == 0)
    return 0;
  constexpr auto max = std::numeric_limits<std::size_t>::max();
  const auto samples = static_cast<std::size_t>(capacityFrames) * channels;
  if (samples > (max - sizeof(RingHeader)) / sizeof(float))
    return 0;
  return sizeof(RingHeader) + samples * sizeof(float);
}

bool RingBuffer::initialize(const std::span<std::byte> storage,
                            const std::uint32_t capacityFrames,
                            const std::uint16_t channels,
                            const std::uint32_t sampleRate) noexcept {
  const auto bytes = requiredBytes(capacityFrames, channels);
  if (bytes == 0 || storage.size() < bytes ||
      reinterpret_cast<std::uintptr_t>(storage.data()) % alignof(RingHeader) != 0)
    return false;

  auto* header = ::new (storage.data()) RingHeader {};
  header->capacityFrames = capacityFrames;
  header->channels = channels;
  header->sampleRate = sampleRate;
  return true;
}

RingBuffer::RingBuffer(const std::span<std::byte> storage) noexcept
    : storageBytes_(storage.size()) {
  if (storage.size() >= sizeof(RingHeader) &&
      reinterpret_cast<std::uintptr_t>(storage.data()) % alignof(RingHeader) == 0) {
    header_ = reinterpret_cast<RingHeader*>(storage.data());
    data_ = reinterpret_cast<float*>(storage.data() + sizeof(RingHeader));
  }
}

bool RingBuffer::isCompatible() const noexcept {
  if (header_ == nullptr || header_->magic != kProtocolMagic ||
      header_->major != kProtocolMajor || header_->channels == 0 ||
      header_->capacityFrames == 0)
    return false;
  return requiredBytes(header_->capacityFrames, header_->channels) <= storageBytes_;
}

std::uint32_t RingBuffer::capacityFrames() const noexcept {
  return isCompatible() ? header_->capacityFrames : 0;
}

std::uint64_t RingBuffer::writePosition() const noexcept {
  return isCompatible() ? header_->writePosition.load(std::memory_order_acquire) : 0;
}

std::uint32_t RingBuffer::availableToRead() const noexcept {
  if (!isCompatible()) return 0;
  const auto write = header_->writePosition.load(std::memory_order_acquire);
  const auto read = header_->readPosition.load(std::memory_order_relaxed);
  return static_cast<std::uint32_t>(std::min<std::uint64_t>(write - std::min(read, write),
                                                            header_->capacityFrames));
}

std::uint32_t RingBuffer::availableToWrite() const noexcept {
  return isCompatible() ? header_->capacityFrames - availableToRead() : 0;
}

std::uint32_t RingBuffer::write(const std::span<const float> interleaved,
                                const std::uint32_t frames) noexcept {
  if (!isCompatible() || interleaved.size() < static_cast<std::size_t>(frames) * header_->channels)
    return 0;
  const auto capacity = header_->capacityFrames;
  const auto channels = header_->channels;
  const auto writePosition = header_->writePosition.load(std::memory_order_relaxed);
  const auto readPosition = header_->readPosition.load(std::memory_order_acquire);
  const auto unread = writePosition - std::min(readPosition, writePosition);
  if (unread + frames > capacity) {
    const auto lost = std::min<std::uint64_t>(unread + frames - capacity, unread);
    header_->droppedFrames.fetch_add(lost, std::memory_order_relaxed);
  }
  for (std::uint32_t frame = 0; frame < frames; ++frame) {
    const auto destinationFrame = (writePosition + frame) % capacity;
    for (std::uint16_t channel = 0; channel < channels; ++channel)
      data_[destinationFrame * channels + channel] =
          interleaved[static_cast<std::size_t>(frame) * channels + channel];
  }
  header_->writePosition.store(writePosition + frames, std::memory_order_release);
  return frames;
}

std::uint32_t RingBuffer::read(const std::span<float> interleaved,
                               const std::uint32_t frames) noexcept {
  if (!isCompatible() || interleaved.size() < static_cast<std::size_t>(frames) * header_->channels)
    return 0;
  const auto write = header_->writePosition.load(std::memory_order_acquire);
  auto readPosition = header_->readPosition.load(std::memory_order_relaxed);
  // Skip anything the producer already overwrote.
  if (write - std::min(readPosition, write) > header_->capacityFrames)
    readPosition = write - header_->capacityFrames;
  const auto readable = static_cast<std::uint32_t>(
      std::min<std::uint64_t>(frames, write - std::min(readPosition, write)));
  for (std::uint32_t frame = 0; frame < readable; ++frame) {
    const auto sourceFrame = (readPosition + frame) % header_->capacityFrames;
    for (std::uint16_t channel = 0; channel < header_->channels; ++channel)
      interleaved[static_cast<std::size_t>(frame) * header_->channels + channel] =
          data_[sourceFrame * header_->channels + channel];
  }
  header_->readPosition.store(readPosition + readable, std::memory_order_release);
  return readable;
}

bool RingBuffer::readAt(const std::uint64_t startFrame, const std::uint32_t frames,
                        const std::span<float> interleaved) const noexcept {
  if (!isCompatible() || interleaved.size() < static_cast<std::size_t>(frames) * header_->channels)
    return false;
  const auto capacity = static_cast<std::uint64_t>(header_->capacityFrames);
  const auto channels = header_->channels;
  const auto before = header_->writePosition.load(std::memory_order_acquire);
  // Keep one block of headroom so a write in progress cannot reach the range.
  const auto oldestSafe = [capacity](const std::uint64_t write) {
    return write > capacity / 2 ? write - capacity / 2 : 0;
  };
  if (startFrame + frames > before || startFrame < oldestSafe(before)) return false;
  for (std::uint32_t frame = 0; frame < frames; ++frame) {
    const auto sourceFrame = (startFrame + frame) % capacity;
    for (std::uint16_t channel = 0; channel < channels; ++channel)
      interleaved[static_cast<std::size_t>(frame) * channels + channel] =
          data_[sourceFrame * channels + channel];
  }
  std::atomic_thread_fence(std::memory_order_acquire);
  const auto after = header_->writePosition.load(std::memory_order_relaxed);
  return startFrame >= oldestSafe(after);
}

void RingBuffer::releaseUpTo(const std::uint64_t frame) noexcept {
  if (!isCompatible()) return;
  auto current = header_->readPosition.load(std::memory_order_relaxed);
  if (frame > current) header_->readPosition.store(frame, std::memory_order_release);
}

void RingBuffer::discardAll() noexcept {
  if (!isCompatible()) return;
  const auto write = header_->writePosition.load(std::memory_order_acquire);
  header_->readPosition.store(write, std::memory_order_release);
}

void RingBuffer::notifyProducer() noexcept {
  if (isCompatible()) header_->producerHeartbeat.fetch_add(1, std::memory_order_relaxed);
}

void RingBuffer::notifyConsumer() noexcept {
  if (isCompatible()) header_->consumerHeartbeat.fetch_add(1, std::memory_order_relaxed);
}

std::uint64_t RingBuffer::producerHeartbeat() const noexcept {
  return isCompatible() ? header_->producerHeartbeat.load(std::memory_order_relaxed) : 0;
}

std::uint64_t RingBuffer::consumerHeartbeat() const noexcept {
  return isCompatible() ? header_->consumerHeartbeat.load(std::memory_order_relaxed) : 0;
}

void RingBuffer::publishClock(const ClockSnapshot& snapshot) noexcept {
  if (!isCompatible()) return;
  // Seqlock writer: odd sequence while the fields are inconsistent.
  const auto sequence = header_->clockSequence.load(std::memory_order_relaxed);
  header_->clockSequence.store(sequence + 1, std::memory_order_relaxed);
  std::atomic_thread_fence(std::memory_order_release);
  header_->clockEpoch.store(snapshot.epoch, std::memory_order_relaxed);
  header_->clockAnchorFrameBits.store(std::bit_cast<std::uint64_t>(snapshot.anchorFrame),
                                      std::memory_order_relaxed);
  header_->clockAnchorTimeNs.store(static_cast<std::uint64_t>(snapshot.anchorTimeNs),
                                   std::memory_order_relaxed);
  header_->clockPeriodBits.store(std::bit_cast<std::uint64_t>(snapshot.periodNs),
                                 std::memory_order_relaxed);
  header_->clockSequence.store(sequence + 2, std::memory_order_release);
}

bool RingBuffer::readClock(ClockSnapshot& snapshot) const noexcept {
  if (!isCompatible()) return false;
  for (int attempt = 0; attempt < 16; ++attempt) {
    const auto first = header_->clockSequence.load(std::memory_order_acquire);
    if ((first & 1U) != 0) continue;
    ClockSnapshot copy;
    copy.epoch = header_->clockEpoch.load(std::memory_order_relaxed);
    copy.anchorFrame = std::bit_cast<double>(
        header_->clockAnchorFrameBits.load(std::memory_order_relaxed));
    copy.anchorTimeNs = static_cast<std::int64_t>(
        header_->clockAnchorTimeNs.load(std::memory_order_relaxed));
    copy.periodNs = std::bit_cast<double>(header_->clockPeriodBits.load(std::memory_order_relaxed));
    std::atomic_thread_fence(std::memory_order_acquire);
    if (header_->clockSequence.load(std::memory_order_relaxed) != first) continue;
    if (first == 0 || !copy.isValid()) return false;
    snapshot = copy;
    return true;
  }
  return false;
}

void RingBuffer::setProducerControl(const ProducerControl& control) noexcept {
  if (!isCompatible()) return;
  header_->producerFlags.store(control.autoCompensation ? kFlagAutoCompensation : 0,
                               std::memory_order_relaxed);
  header_->manualOffsetNs.store(static_cast<std::uint64_t>(control.manualOffsetNs),
                                std::memory_order_relaxed);
  header_->producerLeadNs.store(static_cast<std::uint64_t>(control.leadNs),
                                std::memory_order_relaxed);
}

ProducerControl RingBuffer::producerControl() const noexcept {
  ProducerControl control;
  if (!isCompatible()) return control;
  control.autoCompensation =
      (header_->producerFlags.load(std::memory_order_relaxed) & kFlagAutoCompensation) != 0;
  control.manualOffsetNs =
      static_cast<std::int64_t>(header_->manualOffsetNs.load(std::memory_order_relaxed));
  control.leadNs =
      static_cast<std::int64_t>(header_->producerLeadNs.load(std::memory_order_relaxed));
  return control;
}

void RingBuffer::setConsumerStatus(const ConsumerStatus& status) noexcept {
  if (!isCompatible()) return;
  header_->consumerDelayNs.store(static_cast<std::uint64_t>(status.delayNs),
                                 std::memory_order_relaxed);
  header_->consumerAppliedOffsetNs.store(static_cast<std::uint64_t>(status.appliedOffsetNs),
                                         std::memory_order_relaxed);
  header_->consumerResyncs.store(status.resyncs, std::memory_order_relaxed);
  header_->consumerUnderruns.store(status.underruns, std::memory_order_relaxed);
}

ConsumerStatus RingBuffer::consumerStatus() const noexcept {
  ConsumerStatus status;
  if (!isCompatible()) return status;
  status.delayNs = static_cast<std::int64_t>(header_->consumerDelayNs.load(std::memory_order_relaxed));
  status.appliedOffsetNs =
      static_cast<std::int64_t>(header_->consumerAppliedOffsetNs.load(std::memory_order_relaxed));
  status.resyncs = header_->consumerResyncs.load(std::memory_order_relaxed);
  status.underruns = header_->consumerUnderruns.load(std::memory_order_relaxed);
  return status;
}

} // namespace das::transport
