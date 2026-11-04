// SPDX-License-Identifier: MIT
// Modified for Kinu Stream (2026-11-04): always-write ring, readAt, clock and control tests.
#include <das/transport/RingBuffer.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

namespace {
int failures {};
void check(const bool condition, const char* message) {
  if (!condition) { std::cerr << message << '\n'; ++failures; }
}
}

int main() {
  constexpr std::uint32_t capacity = 256;
  constexpr std::uint16_t channels = 2;
  std::vector<std::byte> raw(das::transport::requiredBytes(capacity, channels) + 63);
  const auto address = reinterpret_cast<std::uintptr_t>(raw.data());
  const auto offset = (alignof(das::transport::RingHeader) - address % alignof(das::transport::RingHeader)) % alignof(das::transport::RingHeader);
  std::span<std::byte> storage(raw.data() + offset, raw.size() - offset);
  check(das::transport::RingBuffer::initialize(storage, capacity, channels), "ring initialisation failed");
  das::transport::RingBuffer ring(storage);
  check(ring.isCompatible(), "compatible ring rejected");

  const std::array<float, 6> input {1, 2, 3, 4, 5, 6};
  std::array<float, 6> output {};
  check(ring.write(input, 3) == 3, "basic write failed");
  check(ring.read(output, 3) == 3, "basic read failed");
  check(input == output, "round trip corrupted samples");

  constexpr std::uint32_t total = 200000;
  std::atomic<bool> corrupt {};
  std::thread producer([&] {
    std::array<float, 2> frame {};
    for (std::uint32_t sequence = 0; sequence < total;) {
      frame = {static_cast<float>(sequence), static_cast<float>(sequence)};
      // The ring overwrites when full; wait for space so this stays a lossless test.
      if (ring.availableToWrite() == 0) { std::this_thread::yield(); continue; }
      if (ring.write(frame, 1) == 1) ++sequence;
    }
  });
  std::thread consumer([&] {
    std::array<float, 2> frame {};
    for (std::uint32_t sequence = 0; sequence < total;) {
      if (ring.read(frame, 1) == 1) {
        if (frame[0] != static_cast<float>(sequence) || frame[1] != frame[0]) corrupt = true;
        ++sequence;
      }
    }
  });
  producer.join();
  consumer.join();
  check(!corrupt, "SPSC stress test corrupted samples");

  // readAt: absolute ranges, rejecting unwritten and overwritten frames.
  {
    std::vector<std::byte> raw2(das::transport::requiredBytes(capacity, channels) + 63);
    const auto address2 = reinterpret_cast<std::uintptr_t>(raw2.data());
    const auto offset2 = (64 - address2 % 64) % 64;
    std::span<std::byte> storage2(raw2.data() + offset2, raw2.size() - offset2);
    check(das::transport::RingBuffer::initialize(storage2, capacity, channels), "init2");
    das::transport::RingBuffer ring2(storage2);
    std::vector<float> frames(capacity * 3 * channels);
    for (std::size_t i = 0; i < frames.size() / channels; ++i)
      frames[i * 2] = frames[i * 2 + 1] = static_cast<float>(i);
    check(ring2.write(std::span<const float>(frames.data(), 100 * channels), 100) == 100,
          "write must store every frame");
    std::array<float, 20> window {};
    check(ring2.readAt(90, 10, window), "readAt of written frames failed");
    check(window[0] == 90.0F && window[18] == 99.0F, "readAt returned wrong frames");
    check(!ring2.readAt(95, 10, window), "readAt accepted unwritten frames");
    // Overwrite: write far past capacity without a consumer.
    check(ring2.write(std::span<const float>(frames.data() + 100 * channels, 600 * channels), 600) == 600,
          "overwriting write must store every frame");
    check(ring2.writePosition() == 700, "write position must count every frame");
    check(ring2.header()->droppedFrames.load() > 0, "overwritten frames must be counted");
    check(!ring2.readAt(100, 10, window), "readAt accepted overwritten frames");
    check(ring2.readAt(690, 10, window) && window[0] == 690.0F, "readAt of recent frames failed");
    std::array<float, 2> one {};
    check(ring2.read(one, 1) == 1 && one[0] >= 700.0F - capacity,
          "sequential read must skip overwritten frames");

    // Clock seqlock and controls.
    das::transport::ClockSnapshot clock;
    check(!ring2.readClock(clock), "an unpublished clock must not read as valid");
    ring2.publishClock({7, 1234.5, 987654321, 20833.3});
    check(ring2.readClock(clock) && clock.epoch == 7 && clock.anchorFrame == 1234.5 &&
              clock.anchorTimeNs == 987654321 && clock.periodNs == 20833.3,
          "clock round trip failed");
    check(std::abs(clock.frameAt(clock.anchorTimeNs + 20833300) - 2234.5) < 1.0e-6,
          "frameAt mapping is wrong");
    ring2.setProducerControl({false, -45'000'000, 30'000'000});
    const auto control = ring2.producerControl();
    check(!control.autoCompensation && control.manualOffsetNs == -45'000'000 &&
              control.leadNs == 30'000'000,
          "producer control round trip failed");

    // Start from a self-consistent snapshot so every read can be checked.
    ring2.publishClock({1, 1.0, 1, 1.0});
    std::atomic<bool> stop {};
    std::atomic<bool> torn {};
    std::thread writer([&] {
      for (std::uint64_t i = 2; !stop.load(); ++i)
        ring2.publishClock({i, static_cast<double>(i), static_cast<std::int64_t>(i), static_cast<double>(i)});
    });
    for (int i = 0; i < 200000; ++i) {
      das::transport::ClockSnapshot snapshot;
      if (ring2.readClock(snapshot)) {
        const auto e = static_cast<double>(snapshot.epoch);
        if (snapshot.anchorFrame != e || static_cast<double>(snapshot.anchorTimeNs) != e ||
            snapshot.periodNs != e)
          torn = true;
      }
    }
    stop = true;
    writer.join();
    check(!torn, "clock seqlock returned a torn snapshot");
  }

  std::vector<std::byte> small(sizeof(das::transport::RingHeader));
  das::transport::RingBuffer invalid(small);
  check(!invalid.isCompatible(), "uninitialised storage accepted");
  return failures == 0 ? 0 : 1;
}
