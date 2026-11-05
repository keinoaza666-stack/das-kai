// SPDX-License-Identifier: MIT
// Live monitor: attaches to the real DAS Kai ring as the consumer (in place of
// OBS, which must not be running) while a real DAW plays, and records what the
// DAW actually does: when the plug-in writes and how much (the host's call
// pattern), the clock model it publishes, and how the receiver copes (breaks,
// late audio, resyncs). Simulations only test the patterns we thought of; this
// measures the host.
//
//   das_kai_live_monitor <seconds> [trace.csv]
//
// One status line per second on stdout. The optional CSV gets every write
// ("w,<uptime ns>,<write position>") and every clock change
// ("c,<uptime ns>,<epoch>,<anchor frame>,<anchor ns>,<period ns>").
#include <das/sync/StreamReceiver.h>
#include <das/transport/NamedSharedMemory.h>
#include <das/transport/RingBuffer.h>

#include <time.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>

namespace {
std::int64_t nowNs() {
  return static_cast<std::int64_t>(clock_gettime_nsec_np(CLOCK_UPTIME_RAW));
}

const char* eventName(const das::sync::StreamReceiver::Event event) {
  using Event = das::sync::StreamReceiver::Event;
  switch (event) {
  case Event::started: return "started (delay ms)";
  case Event::stopped: return "producer stopped";
  case Event::producerRestarted: return "producer re-anchored";
  case Event::timingStep: return "timing step (ms)";
  case Event::lateAudio: return "late audio, delay now (ms)";
  case Event::starved: return "receiver starved (ms)";
  case Event::readFailed: return "read failed";
  }
  return "?";
}

struct Context {
  std::int64_t startNs {};
  int events {};
};

void onEvent(void* context, const das::sync::StreamReceiver::Event event, const double valueMs) {
  auto& c = *static_cast<Context*>(context);
  ++c.events;
  std::printf("  %8.3f s  EVENT %s %.2f\n", static_cast<double>(nowNs() - c.startNs) / 1.0e9,
              eventName(event), valueMs);
  std::fflush(stdout);
}
} // namespace

int main(int argc, char** argv) {
  const auto seconds = argc > 1 ? std::atof(argv[1]) : 60.0;
  std::FILE* trace = argc > 2 ? std::fopen(argv[2], "w") : nullptr;

  const auto bytes = das::transport::requiredBytes(das::transport::kAudioCapacityFrames,
                                                    das::transport::kAudioChannels);
  auto memory = das::transport::NamedSharedMemory::open(das::transport::kObsAudioMappingName, bytes);
  if (!memory.isOpen()) {
    std::fprintf(stderr, "no DAS Kai ring: is the plug-in loaded in a DAW?\n");
    return 2;
  }
  das::transport::RingBuffer ring(memory.storage());
  if (!ring.isCompatible()) {
    std::fprintf(stderr, "ring layout not compatible\n");
    return 2;
  }
  das::sync::StreamReceiver receiver(ring);
  Context context;
  context.startNs = nowNs();
  receiver.setEventHandler(&onEvent, &context);

  std::printf("   time  | writes  frames/write min-max  gap max ms | clock: epoch    ppm  jump ms | "
              "recv: delay shift breaks late resync\n");
  das::sync::StreamReceiver::Block block;
  auto lastWrite = ring.writePosition();
  auto lastWriteNs = nowNs();
  das::transport::ClockSnapshot lastClock {};
  bool haveClock {};
  bool emitted {};
  std::uint64_t lastTs {};
  std::uint32_t lastFrames {};
  // Per-second accumulators.
  int writes {};
  std::uint64_t minDelta = ~0ULL;
  std::uint64_t maxDelta {};
  std::int64_t maxGap {};
  double maxJumpMs {};
  int breaks {};
  int totalBreaks {};
  auto nextReport = context.startNs + 1'000'000'000;
  const auto endNs = context.startNs + static_cast<std::int64_t>(seconds * 1.0e9);

  while (true) {
    const auto now = nowNs();
    if (now >= endNs) break;
    const auto write = ring.writePosition();
    if (write != lastWrite) {
      const auto delta = write - lastWrite;
      ++writes;
      minDelta = std::min(minDelta, delta);
      maxDelta = std::max(maxDelta, delta);
      maxGap = std::max(maxGap, now - lastWriteNs);
      if (trace != nullptr)
        std::fprintf(trace, "w,%lld,%llu\n", static_cast<long long>(now),
                     static_cast<unsigned long long>(write));
      lastWrite = write;
      lastWriteNs = now;
    }
    das::transport::ClockSnapshot clock;
    if (ring.readClock(clock)) {
      const auto changed = !haveClock || clock.epoch != lastClock.epoch ||
                           clock.anchorFrame != lastClock.anchorFrame ||
                           clock.anchorTimeNs != lastClock.anchorTimeNs;
      if (changed) {
        if (haveClock && clock.epoch == lastClock.epoch) {
          // How far the new model moved from where the old one put that frame.
          const auto jumpMs = (static_cast<double>(clock.anchorTimeNs) - lastClock.timeAt(clock.anchorFrame)) / 1.0e6;
          if (std::abs(jumpMs) > std::abs(maxJumpMs)) maxJumpMs = jumpMs;
        }
        if (trace != nullptr)
          std::fprintf(trace, "c,%lld,%llu,%.3f,%lld,%.9f\n", static_cast<long long>(now),
                       static_cast<unsigned long long>(clock.epoch), clock.anchorFrame,
                       static_cast<long long>(clock.anchorTimeNs), clock.periodNs);
        lastClock = clock;
        haveClock = true;
      }
    }
    while (receiver.poll(now, block)) {
      if (emitted && block.timestampNs != lastTs + lastFrames * 1'000'000'000ULL / 48000ULL) {
        ++breaks;
        ++totalBreaks;
      }
      lastTs = block.timestampNs;
      lastFrames = block.frames;
      emitted = true;
    }
    if (now >= nextReport) {
      nextReport += 1'000'000'000;
      const auto ppm = haveClock ? ((1.0e9 / 48000.0) / lastClock.periodNs - 1.0) * 1.0e6 : 0.0;
      std::printf("%8.1f | %6d  %6llu-%-6llu  %9.1f | %12llu %+7.1f %+8.2f | %6.0f %6.1f %5d %5llu %6llu\n",
                  static_cast<double>(now - context.startNs) / 1.0e9, writes,
                  static_cast<unsigned long long>(writes > 0 ? minDelta : 0),
                  static_cast<unsigned long long>(maxDelta), static_cast<double>(maxGap) / 1.0e6,
                  static_cast<unsigned long long>(lastClock.epoch), ppm, maxJumpMs,
                  static_cast<double>(receiver.delayNs()) / 1.0e6,
                  static_cast<double>(receiver.appliedOffsetNs()) / 1.0e6, breaks,
                  static_cast<unsigned long long>(receiver.underruns()),
                  static_cast<unsigned long long>(receiver.resyncs()));
      std::fflush(stdout);
      writes = 0;
      minDelta = ~0ULL;
      maxDelta = 0;
      maxGap = 0;
      maxJumpMs = 0.0;
      breaks = 0;
    }
    std::this_thread::sleep_for(std::chrono::microseconds(500));
  }
  std::printf("total: breaks %d, late audio %llu, resyncs %llu, events %d\n", totalBreaks,
              static_cast<unsigned long long>(receiver.underruns()),
              static_cast<unsigned long long>(receiver.resyncs()), context.events);
  if (trace != nullptr) std::fclose(trace);
  return 0;
}
