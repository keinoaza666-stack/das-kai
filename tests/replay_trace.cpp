// Replays a host's real call pattern, recorded by das_kai_live_monitor, through
// the current sender and receiver: every recorded write becomes a
// StreamSender::process call of the same size at the same time, and the
// receiver polls every millisecond as in OBS. A recording made with any
// version of the plug-in (the writes reflect only when and how much the host
// asked for) thereby tests the current code against that host.
//
//   das_kai_replay_trace <trace.csv> [host block frames at 48 kHz]
//
// Writes are 48 kHz ring frames, so the replayed sender runs at 48 kHz. The
// monitor sees blocks processed back to back as one write, so each write is
// split into the host's block size (the greatest common divisor of all write
// sizes, or the block given on the command line: a host that always writes
// whole equal chunks looks like one block per chunk otherwise, and the
// replay would hide the chunk's sawtooth), all at the write's time. The timing
// resolution is the monitor's polling interval (0.5 ms). A host at 44.1 kHz
// writes uneven 48 kHz amounts; its writes are replayed whole.
#include <das/audio/StreamSender.h>
#include <das/sync/StreamReceiver.h>
#include <das/transport/RingBuffer.h>

#include <cmath>
#include <cstdint>
#include <numeric>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <vector>

namespace {
struct HeapRing {
  HeapRing() {
    const auto bytes = das::transport::requiredBytes(das::transport::kAudioCapacityFrames,
                                                      das::transport::kAudioChannels);
    raw.resize(bytes + 64);
    const auto address = reinterpret_cast<std::uintptr_t>(raw.data());
    const auto offset = (64 - address % 64) % 64;
    storage = std::span<std::byte>(raw.data() + offset, bytes);
    das::transport::RingBuffer::initialize(storage, das::transport::kAudioCapacityFrames,
                                           das::transport::kAudioChannels,
                                           das::transport::kAudioSampleRate);
    ring = std::make_unique<das::transport::RingBuffer>(storage);
  }
  std::vector<std::byte> raw;
  std::span<std::byte> storage;
  std::unique_ptr<das::transport::RingBuffer> ring;
};

struct Write {
  std::int64_t timeNs {};
  std::uint64_t position {};
};
} // namespace

int main(const int argc, char** argv) {
  if (argc != 2 && argc != 3) {
    std::fprintf(stderr, "usage: das_kai_replay_trace <trace.csv> [block frames]\n");
    return 1;
  }
  std::ifstream input(argv[1]);
  std::vector<Write> writes;
  for (std::string line; std::getline(input, line);) {
    if (line.rfind("w,", 0) != 0) continue;
    std::istringstream fields(line.substr(2));
    Write write;
    char comma {};
    fields >> write.timeNs >> comma >> write.position;
    if (fields) writes.push_back(write);
  }
  if (writes.size() < 2) {
    std::fprintf(stderr, "no writes in %s\n", argv[1]);
    return 2;
  }

  std::uint64_t block {};
  for (std::size_t i = 1; i < writes.size(); ++i)
    block = std::gcd(block, writes[i].position - writes[i - 1].position);
  if (argc == 3) block = static_cast<std::uint64_t>(std::atoll(argv[2]));
  if (block < 32) block = 0; // uneven writes: replay each one whole
  std::uint64_t largest {};
  for (std::size_t i = 1; i < writes.size(); ++i)
    largest = std::max(largest, writes[i].position - writes[i - 1].position);
  const auto maximumBlock = static_cast<std::size_t>(block > 0 ? block : std::min<std::uint64_t>(largest, 16384));
  std::printf("host block %llu frames (0 = uneven)\n", static_cast<unsigned long long>(block));

  HeapRing shared;
  das::audio::StreamSender sender;
  sender.prepare(48000.0, maximumBlock, shared.ring.get());
  das::sync::StreamReceiver receiver(*shared.ring);
  std::vector<float> silence(maximumBlock, 0.0F);

  const auto t0 = writes.front().timeNs;
  auto pollTime = t0;
  auto lastEpoch = sender.epoch();
  int anchors {};
  int breaks {};
  bool emitted {};
  std::uint64_t lastTs {};
  std::uint32_t lastFrames {};
  das::sync::StreamReceiver::Block out;
  double nextReport = 5.0;
  std::printf("  time s | drift ppm locked | epochs | delay ms breaks late resyncs\n");
  for (std::size_t i = 1; i < writes.size(); ++i) {
    const auto& write = writes[i];
    // Gaps longer than the producer timeout: the host stopped; replay that.
    while (pollTime < write.timeNs) {
      while (receiver.poll(pollTime, out)) {
        if (emitted && out.timestampNs != lastTs + lastFrames * 1'000'000'000ULL / 48000ULL) ++breaks;
        lastTs = out.timestampNs;
        lastFrames = out.frames;
        emitted = true;
      }
      const auto seconds = static_cast<double>(pollTime - t0) / 1.0e9;
      if (seconds >= nextReport) {
        nextReport += 5.0;
        std::printf("%8.1f | %+8.1f %6s | %6d | %8.0f %6d %4llu %7llu\n", seconds, sender.driftPpm(),
                    sender.clockLocked() ? "yes" : "no", anchors,
                    static_cast<double>(receiver.delayNs()) / 1.0e6, breaks,
                    static_cast<unsigned long long>(receiver.underruns()),
                    static_cast<unsigned long long>(receiver.resyncs()));
      }
      pollTime += 1'000'000;
    }
    auto frames = static_cast<std::size_t>(write.position - writes[i - 1].position);
    while (frames > 0) {
      const auto chunk = std::min(frames, maximumBlock);
      sender.process(silence.data(), silence.data(), chunk, write.timeNs);
      frames -= chunk;
    }
    if (sender.epoch() != lastEpoch) {
      ++anchors;
      lastEpoch = sender.epoch();
    }
  }
  std::printf("replayed %zu writes over %.1f s: re-anchors %d, breaks %d, late %llu, resyncs %llu, "
              "final drift %+.1f ppm (%s)\n",
              writes.size(), static_cast<double>(writes.back().timeNs - t0) / 1.0e9, anchors, breaks,
              static_cast<unsigned long long>(receiver.underruns()),
              static_cast<unsigned long long>(receiver.resyncs()), sender.driftPpm(),
              sender.clockLocked() ? "locked" : "not locked");
  return 0;
}
