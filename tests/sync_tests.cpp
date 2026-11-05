// SPDX-License-Identifier: GPL-3.0-or-later
// DAS Kai sync tests: clock estimator, interpolator quality and an
// end-to-end simulation (StreamSender -> RingBuffer -> StreamReceiver) on a
// simulated clock, so minutes of drifting audio run in seconds.
#include <das/audio/StreamSender.h>
#include <das/sync/FrameClockEstimator.h>
#include <das/sync/SincInterpolator.h>
#include <das/sync/StreamReceiver.h>
#include <das/transport/RingBuffer.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <numbers>
#include <random>
#include <string>
#include <vector>

namespace {
int failures {};
void check(const bool condition, const std::string& message) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", message.c_str());
    ++failures;
  }
}

constexpr double kPi = std::numbers::pi;

// ---------------------------------------------------------------- interpolator
void testInterpolator() {
  das::sync::SincInterpolator interpolator;

  // Fraction 0 must pass samples through bit-exactly.
  std::vector<float> noise(256);
  std::mt19937 random(1);
  std::uniform_real_distribution<float> uniform(-1.0F, 1.0F);
  for (auto& value : noise) value = uniform(random);
  bool exact = true;
  for (std::size_t base = 40; base < 200; ++base)
    exact = exact && interpolator.at(noise.data(), base, 1, 0.0) == noise[base];
  check(exact, "interpolator must be bit-exact at integer positions");

  // Fractional reads of pure tones at a drifting ratio, against the exact value.
  const struct { double frequency; double limitDb; } cases[] {
      {1000.0, -100.0}, {10000.0, -95.0}, {16000.0, -90.0}, {20000.0, -60.0}};
  for (const auto& tone : cases) {
    constexpr std::size_t length = 40000;
    std::vector<float> signal(length);
    for (std::size_t n = 0; n < length; ++n)
      signal[n] = static_cast<float>(0.5 * std::sin(2.0 * kPi * tone.frequency * n / 48000.0 + 0.3));
    double errorEnergy {};
    double signalEnergy {};
    for (std::size_t i = 0; i < 30000; ++i) {
      const auto position = 100.37 + static_cast<double>(i) * 1.0005;
      const auto base = static_cast<std::size_t>(std::floor(position));
      const auto value = interpolator.at(signal.data(), base, 1, position - static_cast<double>(base));
      const auto ideal = 0.5 * std::sin(2.0 * kPi * tone.frequency * position / 48000.0 + 0.3);
      errorEnergy += (value - ideal) * (value - ideal);
      signalEnergy += ideal * ideal;
    }
    const auto db = 10.0 * std::log10(errorEnergy / signalEnergy);
    std::printf("interpolator %5.0f Hz error %.1f dB (limit %.0f)\n", tone.frequency, db, tone.limitDb);
    check(db < tone.limitDb, "interpolator error too high at " + std::to_string(tone.frequency));
  }
}

// ------------------------------------------------------------------- estimator
void testEstimator() {
  using das::sync::FrameClockEstimator;
  constexpr double rate = 44100.0;
  constexpr double ppm = 250.0;
  constexpr std::int64_t t0 = 500'000'000'000'000;
  const double trueRate = rate * (1.0 + ppm * 1.0e-6);
  FrameClockEstimator clock;
  clock.reset(1.0e9 / 48000.0);
  std::mt19937 random(2);
  std::uniform_real_distribution<double> jitter(0.0, 3.0e6);
  int anchors {};
  double worstTimeError {};
  for (int block = 1; block <= 60 * 86; ++block) {
    const auto samples = static_cast<double>(block) * 512.0;
    const auto trueTime = static_cast<double>(t0) + samples / trueRate * 1.0e9;
    const auto observed = static_cast<std::int64_t>(trueTime + jitter(random));
    const auto frame = samples * 48000.0 / rate;
    if (clock.observe(frame, observed) == FrameClockEstimator::Update::anchored) ++anchors;
    if (block > 20 * 86) {
      // The model follows the median call: true time plus the median jitter
      // (1.5 ms).
      const auto modelTime = static_cast<double>(clock.anchorTimeNs()) +
                             (frame - clock.anchorFrame()) * clock.periodNs();
      worstTimeError = std::max(worstTimeError, std::abs(modelTime - (trueTime + 1.5e6)));
    }
  }
  std::printf("estimator drift %.2f ppm (true %.0f), worst time error %.3f ms, anchors %d\n",
              clock.driftPpm(), ppm, worstTimeError / 1.0e6, anchors);
  check(anchors == 1, "estimator re-anchored during steady jitter");
  check(std::abs(clock.driftPpm() - ppm) < 5.0, "estimator drift estimate is off");
  check(worstTimeError < 0.5e6, "estimator model time error above 0.5 ms");

  // Pre-roll: ten blocks processed instantly, then real time. The slope must
  // still converge to the true rate.
  {
    FrameClockEstimator preroll;
    preroll.reset(1.0e9 / 48000.0);
    std::int64_t now = t0;
    int block = 0;
    for (; block < 10; ++block) preroll.observe((block + 1) * 512.0 * 48000.0 / rate, now += 50'000);
    const auto origin = now;
    for (int i = 1; i <= 30 * 86; ++i) {
      const auto at = origin + static_cast<std::int64_t>(i * 512.0 / trueRate * 1.0e9 + jitter(random));
      preroll.observe((block + i) * 512.0 * 48000.0 / rate, at);
    }
    std::printf("estimator after pre-roll: drift %.2f ppm\n", preroll.driftPpm());
    check(std::abs(preroll.driftPpm() - ppm) < 10.0, "pre-roll burst biased the clock estimate");
  }

  // A single late block does not move the model; a lasting jump moves the
  // line (keeping the slope and the lock); a long gap re-anchors.
  auto frameAt = [](int block) { return block * 512.0 * 48000.0 / rate; };
  // Same jitter as above, so the level the model follows does not change.
  auto timeAt = [&](int block) {
    return static_cast<std::int64_t>(static_cast<double>(t0) + block * 512.0 / trueRate * 1.0e9 + jitter(random));
  };
  auto modelError = [&](int block, double expectedOffsetNs) {
    const auto modelTime = static_cast<double>(clock.anchorTimeNs()) +
                           (frameAt(block) - clock.anchorFrame()) * clock.periodNs();
    const auto trueTime = static_cast<double>(t0) + block * 512.0 / trueRate * 1.0e9;
    return std::abs(modelTime - (trueTime + expectedOffsetNs));
  };
  auto block = 60 * 86 + 1;
  const auto driftBefore = clock.driftPpm();
  bool movedByOutlier {};
  for (int i = 0; i < 2 * 86; ++i, ++block) {
    const auto late = i == 20 ? 60'000'000 : 0;
    const auto update = clock.observe(frameAt(block), timeAt(block) + late);
    movedByOutlier |= update == FrameClockEstimator::Update::anchored ||
                      update == FrameClockEstimator::Update::shifted;
  }
  check(!movedByOutlier, "a single 60 ms late block must not shift or re-anchor the model");
  check(modelError(block, 1.5e6) < 0.5e6, "a single late block moved the model");

  int shiftedAfter = -1;
  for (int i = 0; i < 3 * 86; ++i, ++block) {
    const auto update = clock.observe(frameAt(block), timeAt(block) + 60'000'000);
    check(update != FrameClockEstimator::Update::anchored, "a lasting 60 ms jump must not re-anchor");
    if (update == FrameClockEstimator::Update::shifted && shiftedAfter < 0) shiftedAfter = i;
  }
  std::printf("estimator: 60 ms step absorbed after %d blocks (%.0f ms), step %.1f ms, drift %.2f ppm\n",
              shiftedAfter, shiftedAfter * 512.0 / rate * 1000.0, clock.lastStepNs() / 1.0e6,
              clock.driftPpm());
  check(shiftedAfter >= 0 && shiftedAfter * 512.0 / rate < 1.6, "a lasting 60 ms jump must shift the line within 1.6 s");
  check(clock.isLocked(), "a step must not drop the lock");
  // Within the estimate's own noise at 3 ms of jitter.
  check(std::abs(clock.driftPpm() - driftBefore) < 2.0, "a step must not change the drift estimate");
  check(modelError(block, 61.5e6) < 0.5e6, "after a step the model must sit on the new level");
  check(clock.observe(frameAt(block + 1), timeAt(block + 1) + 600'000'000) ==
            FrameClockEstimator::Update::anchored,
        "a 600 ms gap must re-anchor immediately");
}

// ------------------------------------------------------------------ simulation
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

struct Event {
  double atSeconds {-1.0};
  double value {};
};

struct Scenario {
  const char* name {};
  double dawRate {44100.0};
  double ppm {};
  std::size_t block {512};
  double jitterMs {3.0};
  double seconds {60.0};
  int burstEvery {};         // every n-th callback is late by burstMs
  double burstMs {};
  Event manualOffsetMs;      // set the manual offset at a time
  Event autoOff;             // switch auto compensation off at a time
  Event pause;               // stop calling the plug-in for value seconds
  Event clockReadFailure;    // make one receiver poll lose the seqlock race
};

struct BlockRecord {
  double simSeconds {};
  double timingErrorMs {};
};

struct Result {
  std::vector<BlockRecord> blocks;
  std::vector<std::pair<double, double>> breaks; // (sim seconds, deviation ms)
  double worstResidual {};
  double worstResidualAt {};
  std::size_t batches {};
  std::uint64_t underruns {};
  std::uint64_t resyncs {};
  double initialDelayMs {};
  double finalDelayMs {};
  double appliedOffsetMs {};
  double driftPpm {};
};

Result simulate(const Scenario& scenario) {
  HeapRing shared;
  auto& ring = *shared.ring;
  das::audio::StreamSender sender;
  sender.prepare(scenario.dawRate, scenario.block, &ring);
  das::sync::StreamReceiver receiver(ring);

  constexpr std::int64_t t0 = 500'000'000'000'000; // arbitrary uptime origin
  const auto trueRate = scenario.dawRate * (1.0 + scenario.ppm * 1.0e-6);
  std::mt19937 random(7);
  std::uniform_real_distribution<double> jitter(0.0, scenario.jitterMs * 1.0e6);
  std::uniform_real_distribution<double> wake(0.0, 0.8e6);

  std::vector<float> left(scenario.block);
  std::vector<float> right(scenario.block);
  std::uint64_t callback {};
  std::uint64_t sample {};
  double pausedNs {};
  bool paused {};
  bool manualDone {};
  bool autoDone {};
  bool clockFailureDone {};
  auto* header = reinterpret_cast<das::transport::RingHeader*>(shared.storage.data());
  std::int64_t pollTime = t0;
  const auto endTime = t0 + static_cast<std::int64_t>(scenario.seconds * 1.0e9);

  Result result;
  bool havePrevious {};
  std::uint64_t previousTs {};
  std::uint32_t previousFrames {};
  std::size_t batches {};
  float lastTwo[2] {};
  int continuous {};
  const auto omegaT = 2.0 * kPi * 1000.0 / 48000.0;
  const auto recurrence = 2.0 * std::cos(omegaT);

  auto nextCallbackTime = [&]() {
    const auto end = static_cast<double>(sample + scenario.block);
    auto when = static_cast<double>(t0) + end / trueRate * 1.0e9 + pausedNs + jitter(random);
    if (scenario.burstEvery > 0 && callback % static_cast<std::uint64_t>(scenario.burstEvery) == 0 &&
        callback > 0)
      when += scenario.burstMs * 1.0e6;
    return static_cast<std::int64_t>(when);
  };
  auto producerTime = nextCallbackTime();

  das::sync::StreamReceiver::Block block;
  while (pollTime < endTime) {
    if (producerTime <= pollTime) {
      const auto simSeconds = static_cast<double>(producerTime - t0) / 1.0e9;
      if (scenario.pause.atSeconds >= 0.0 && !paused && simSeconds >= scenario.pause.atSeconds) {
        // The engine stops: no callbacks, and the samples of that time never exist.
        paused = true;
        pausedNs += scenario.pause.value * 1.0e9;
        producerTime = nextCallbackTime();
        continue;
      }
      if (scenario.manualOffsetMs.atSeconds >= 0.0 && !manualDone &&
          simSeconds >= scenario.manualOffsetMs.atSeconds) {
        manualDone = true;
      }
      if (scenario.autoOff.atSeconds >= 0.0 && !autoDone && simSeconds >= scenario.autoOff.atSeconds)
        autoDone = true;
      sender.setControl(!autoDone, manualDone ? static_cast<std::int64_t>(
                                                    scenario.manualOffsetMs.value * 1.0e6)
                                              : 0);
      for (std::size_t i = 0; i < scenario.block; ++i) {
        const auto trueNs = (static_cast<double>(sample + i) / trueRate) * 1.0e9 + pausedNs;
        left[i] = static_cast<float>(0.5 * std::sin(2.0 * kPi * 4.0 * trueNs / 1.0e9));
        right[i] = static_cast<float>(0.5 * std::sin(2.0 * kPi * 1000.0 * trueNs / 1.0e9));
      }
      sender.process(left.data(), right.data(), scenario.block, producerTime);
      sample += scenario.block;
      ++callback;
      producerTime = nextCallbackTime();
      continue;
    }

    // Hold the clock sequence odd (a writer "in progress") for this poll only.
    const auto failClock = scenario.clockReadFailure.atSeconds >= 0.0 && !clockFailureDone &&
                           static_cast<double>(pollTime - t0) / 1.0e9 >= scenario.clockReadFailure.atSeconds;
    if (failClock) header->clockSequence.fetch_add(1);
    while (receiver.poll(pollTime, block)) {
      const auto ts = block.timestampNs;
      const auto simSeconds = static_cast<double>(static_cast<std::int64_t>(ts) - t0) / 1.0e9;
      if (result.blocks.empty()) result.initialDelayMs = static_cast<double>(receiver.delayNs()) / 1.0e6;
      if (havePrevious) {
        const auto deviation = static_cast<double>(
            static_cast<std::int64_t>(ts) - static_cast<std::int64_t>(previousTs) -
            static_cast<std::int64_t>(previousFrames) * 1'000'000'000LL / 48000);
        if (deviation != 0.0) {
          result.breaks.emplace_back(simSeconds, deviation / 1.0e6);
          continuous = 0;
        }
      }
      // Timing error from the 4 Hz reference: fit A sin + B cos on the block's
      // own timestamps; a delay e gives A = 0.5 cos(we), B = -0.5 sin(we).
      double ss {}, sc {}, cc {}, ys {}, yc {};
      if (block.frames != das::sync::StreamReceiver::kBlockFrames) ++batches;
      for (std::uint32_t i = 0; i < block.frames; ++i) {
        const auto t = (static_cast<double>(static_cast<std::int64_t>(ts) - t0) + i * 1.0e9 / 48000.0) / 1.0e9;
        const auto s = std::sin(2.0 * kPi * 4.0 * t);
        const auto c = std::cos(2.0 * kPi * 4.0 * t);
        const auto y = static_cast<double>(block.samples[i * 2]);
        ss += s * s; sc += s * c; cc += c * c; ys += y * s; yc += y * c;
      }
      const auto determinant = ss * cc - sc * sc;
      const auto a = (ys * cc - yc * sc) / determinant;
      const auto b = (yc * ss - ys * sc) / determinant;
      const auto errorMs = std::atan2(-b, a) / (2.0 * kPi * 4.0) * 1000.0;
      result.blocks.push_back({simSeconds, errorMs});

      // Continuity of the 1 kHz channel across the whole emitted stream:
      // y[n+1] - 2cos(wT) y[n] + y[n-1] = 0 for a clean sine.
      for (std::uint32_t i = 0; i < block.frames; ++i) {
        const auto y = block.samples[i * 2 + 1];
        if (continuous >= 2 && simSeconds > 2.0) {
          const auto residual = std::abs(static_cast<double>(y) - recurrence * lastTwo[1] + lastTwo[0]);
          if (residual > result.worstResidual) {
            result.worstResidual = residual;
            result.worstResidualAt = simSeconds;
          }
        }
        lastTwo[0] = lastTwo[1];
        lastTwo[1] = y;
        ++continuous;
      }
      previousTs = ts;
      previousFrames = block.frames;
      havePrevious = true;
    }
    if (failClock) {
      header->clockSequence.fetch_add(1);
      clockFailureDone = true;
    }
    pollTime = std::max(pollTime + 1'000'000, static_cast<std::int64_t>(receiver.nextWakeNs(pollTime))) +
               static_cast<std::int64_t>(wake(random));
  }
  result.batches = batches;
  result.underruns = receiver.underruns();
  result.resyncs = receiver.resyncs();
  result.finalDelayMs = static_cast<double>(receiver.delayNs()) / 1.0e6;
  result.appliedOffsetMs = static_cast<double>(receiver.appliedOffsetNs()) / 1.0e6;
  result.driftPpm = sender.driftPpm();
  return result;
}

struct Stats {
  double mean {};
  double worst {};
  std::size_t count {};
};

Stats errorStats(const Result& result, const double from, const double to) {
  Stats stats;
  double sum {};
  for (const auto& block : result.blocks)
    if (block.simSeconds >= from && block.simSeconds < to) { sum += block.timingErrorMs; ++stats.count; }
  if (stats.count == 0) return stats;
  stats.mean = sum / static_cast<double>(stats.count);
  for (const auto& block : result.blocks)
    if (block.simSeconds >= from && block.simSeconds < to)
      stats.worst = std::max(stats.worst, std::abs(block.timingErrorMs - stats.mean));
  return stats;
}

void report(const Scenario& scenario, const Result& result) {
  std::printf("[%s] blocks %zu breaks %zu underruns %llu resyncs %llu delay %.0f->%.0f ms "
              "drift %.1f ppm worst residual %.2e at %.2f s\n",
              scenario.name, result.blocks.size(), result.breaks.size(),
              static_cast<unsigned long long>(result.underruns),
              static_cast<unsigned long long>(result.resyncs), result.initialDelayMs,
              result.finalDelayMs, result.driftPpm, result.worstResidual, result.worstResidualAt);
  for (const auto& [at, deviation] : result.breaks)
    std::printf("    timestamp break at %.2f s: %+.1f ms\n", at, deviation);
}

void testDriftAbsorption(const char* name, double dawRate, double ppm, std::size_t blockFrames,
                         double jitterMs, double seconds) {
  Scenario scenario;
  scenario.name = name;
  scenario.dawRate = dawRate;
  scenario.ppm = ppm;
  scenario.block = blockFrames;
  scenario.jitterMs = jitterMs;
  scenario.seconds = seconds;
  const auto result = simulate(scenario);
  report(scenario, result);
  const auto early = errorStats(result, 10.0, 70.0);
  const auto late = errorStats(result, seconds - 60.0, seconds);
  const auto all = errorStats(result, 10.0, seconds);
  std::printf("    timing error: mean %.3f ms (early %.3f, late %.3f), worst deviation %.3f ms\n",
              all.mean, early.mean, late.mean, all.worst);
  const auto expected = jitterMs / 2.0; // timestamps carry the median callback lateness
  check(result.breaks.empty(), std::string(name) + ": timestamps must stay continuous");
  check(result.underruns == 0 && result.resyncs == 0, std::string(name) + ": no underrun or resync");
  check(std::abs(all.mean - expected) < 0.4, std::string(name) + ": timestamps off from DAW time");
  check(all.worst < 0.5, std::string(name) + ": timing wanders more than 0.5 ms");
  check(std::abs(late.mean - early.mean) < 0.2, std::string(name) + ": drift accumulated");
  check(result.worstResidual < 1.0e-4, std::string(name) + ": discontinuity in the audio");
  check(std::abs(result.driftPpm - ppm) < 5.0, std::string(name) + ": drift estimate off");
  check(result.finalDelayMs == result.initialDelayMs, std::string(name) + ": delay had to grow");
}

void testOffsetChanges() {
  Scenario scenario;
  scenario.name = "offset changes";
  scenario.ppm = 100.0;
  scenario.seconds = 120.0;
  scenario.manualOffsetMs = {40.0, 45.0};
  scenario.autoOff = {80.0, 1.0};
  const auto result = simulate(scenario);
  report(scenario, result);
  const auto before = errorStats(result, 10.0, 39.0);
  const auto manual = errorStats(result, 42.0, 79.0);
  const auto noAuto = errorStats(result, 82.0, 120.0);
  std::printf("    timing error: before %.3f, manual +45 %.3f, auto off %.3f ms (delay %.0f)\n",
              before.mean, manual.mean, noAuto.mean, result.finalDelayMs);
  check(result.breaks.size() == 2, "offset changes: expected exactly two timestamp breaks");
  // Start + two breaks: each must reach OBS as one packet of at least a mixing tick.
  check(result.batches == 3, "offset changes: every break must start with a batch packet");
  static_assert(das::sync::StreamReceiver::kMaximumPacketFrames >= 1024);
  for (const auto& [at, deviation] : result.breaks)
    check(std::abs(deviation) >= 80.0, "offset changes: a break OBS would smooth away");
  check(std::abs(manual.mean - (before.mean - 45.0)) < 0.3,
        "offset changes: +45 ms manual offset must place audio 45 ms earlier");
  check(std::abs(noAuto.mean - (manual.mean + result.finalDelayMs)) < 0.3,
        "offset changes: auto off must drop exactly the receiver delay");
  check(result.worstResidual < 1.0e-4, "offset changes: discontinuity inside continuous audio");
}

void testPause() {
  Scenario scenario;
  scenario.name = "engine pause";
  scenario.ppm = 50.0;
  scenario.seconds = 120.0;
  scenario.pause = {50.0, 2.0};
  const auto result = simulate(scenario);
  report(scenario, result);
  const auto before = errorStats(result, 10.0, 49.0);
  const auto after = errorStats(result, 60.0, 120.0);
  std::printf("    timing error: before %.3f, after %.3f ms\n", before.mean, after.mean);
  check(result.breaks.size() == 1, "engine pause: expected one timestamp break at resume");
  // A stopping DAW writes nothing more; that is not late audio (an earlier
  // receiver grew the delay by 180 ms before it noticed the stop).
  check(result.underruns == 0, "engine pause: a stopping DAW must not count as late audio");
  check(std::abs(after.mean - before.mean) < 0.4, "engine pause: alignment must recover");
  check(after.worst < 0.5, "engine pause: timing must be stable after resume");
  // The first blocks after the engine stops look like a late producer and may
  // bump the delay; the restart must bring it back to the initial value.
  check(result.finalDelayMs == result.initialDelayMs,
        "engine pause: a stopped DAW must not leave the delay grown");
}

void testClockReadFailure() {
  // A reader can lose the seqlock race with the writer; that must not be
  // mistaken for the producer going away.
  Scenario scenario;
  scenario.name = "clock read failure";
  scenario.ppm = 80.0;
  scenario.seconds = 40.0;
  scenario.clockReadFailure = {20.0, 0.0};
  const auto result = simulate(scenario);
  report(scenario, result);
  check(result.breaks.empty(), "clock read failure: output must not restart");
  check(result.resyncs == 0, "clock read failure: no resync");
}

void testOffsetCeiling() {
  // Even an out-of-range manual value must keep the total shift inside what
  // OBS can buffer (960 ms).
  Scenario scenario;
  scenario.name = "offset ceiling";
  scenario.seconds = 20.0;
  scenario.manualOffsetMs = {5.0, 1000.0};
  const auto result = simulate(scenario);
  report(scenario, result);
  std::printf("    applied offset %.1f ms\n", result.appliedOffsetMs);
  check(std::abs(result.appliedOffsetMs - 850.0) < 0.001, "offset ceiling: total shift must stop at 850 ms");
}

void testBursts() {
  Scenario scenario;
  scenario.name = "late callbacks";
  scenario.ppm = -150.0;
  scenario.jitterMs = 1.0;
  scenario.seconds = 180.0;
  scenario.burstEvery = 300;
  scenario.burstMs = 25.0;
  const auto result = simulate(scenario);
  report(scenario, result);
  std::size_t lateBreaks {};
  for (const auto& entry : result.breaks)
    if (entry.first > 90.0) ++lateBreaks;
  check(result.underruns <= 3, "late callbacks: too many underruns");
  check(lateBreaks == 0, "late callbacks: the delay must settle in the first half");
  // 25 ms late callbacks need about 25 ms more delay (plus one 10 ms step).
  check(result.finalDelayMs <= result.initialDelayMs + 40.0, "late callbacks: delay grew too far");
}

// Host scheduling patterns: how the calls of a DAW relate to the hardware.
// Cubase's ASIO-Guard processes ahead of the hardware in chunks, fills its
// pre-fetch at start and may jump further ahead or fall back; none of that is
// clock drift. The chunk sizes include ones whose period beats against the
// estimator's segments (6 and 8 x 512, 4 x 2048), which broke an earlier
// version, and chunks spanning more than the reset threshold.
struct HostPattern {
  const char* name {};
  double rate {48000.0};
  std::size_t block {256};
  double ppm {-15.0};
  int chunk {1};          // blocks processed back to back
  double prefillMs {};    // the host runs this far ahead at start...
  double prefillOverS {}; // ...reached gradually over this time (0 = at once)
  double jumpMs {};       // extra audio processed at once and never played (re-render)...
  double jumpEveryS {};   // ...this often
  double stepAtS {-1.0};  // from then on the host calls stepMs later
  double stepMs {};
  double seconds {120.0};
  // Patterns from the second review:
  int chunkMax {};          // > chunk: each chunk has a random size in [chunk, chunkMax]
  double chunkChangeAtS {-1.0}; // from then on chunks have chunkAfter blocks
  int chunkAfter {1};
  double earlyEveryS {};    // one block this often is called earlyMs early
  double earlyMs {};
  double switchEveryS {};   // the host alternates between real time and
  double switchAheadMs {};  // running switchAheadMs ahead this often
  double jitterToMs {-1.0}; // >= 0: jitter spreads both ways, from 0.5 ms wide
                            // to this wide over the run
};

struct HostResult {
  double finalPpm {};
  bool finalLocked {};
  double worstPpmError {}; // after the settle time, while locked
  int unlockedSamples {};  // seconds after the settle time not locked
  double maxShownPpm {};   // largest |ppm| ever shown as locked
  int breaks {};
  int breaksAfter30s {};
  int epochs {};
  double initialDelayMs {};
  double finalDelayMs {};
};

HostResult simulateHost(const HostPattern& p, const double settleSeconds) {
  HeapRing shared;
  das::audio::StreamSender sender;
  sender.prepare(p.rate, p.block, shared.ring.get());
  das::sync::StreamReceiver receiver(*shared.ring);
  constexpr std::int64_t t0 = 500'000'000'000'000;
  const auto trueRate = p.rate * (1.0 + p.ppm * 1.0e-6);
  const auto blockNs = static_cast<double>(p.block) / trueRate * 1.0e9;
  std::mt19937 random(3);
  std::uniform_real_distribution<double> unit(0.0, 1.0);
  std::vector<float> silence(p.block, 0.0F);

  // Called with consecutive indices; tracks the current chunk.
  std::uint64_t chunkFirst {};
  std::uint64_t chunkSize {};
  auto nextEarly = p.earlyEveryS > 0.0 ? p.earlyEveryS : 1.0e9;
  auto nextJump = p.jumpEveryS > 0.0 ? p.jumpEveryS : 1.0e9;
  auto callTime = [&](const std::uint64_t index) {
    if (index >= chunkFirst + chunkSize) {
      chunkFirst = index;
      const auto seconds = static_cast<double>(index) * blockNs / 1.0e9;
      auto size = p.chunkChangeAtS >= 0.0 && seconds >= p.chunkChangeAtS ? p.chunkAfter : p.chunk;
      if (p.chunkMax > p.chunk)
        size = p.chunk + static_cast<int>(unit(random) * static_cast<double>(p.chunkMax - p.chunk + 1));
      chunkSize = static_cast<std::uint64_t>(std::max(1, size));
    }
    // Re-rendered audio adds frames but no playout time; the calls keep
    // their place relative to the hardware.
    const auto playout = static_cast<double>(chunkFirst + 1) * blockNs;
    const auto seconds = playout / 1.0e9;
    auto ahead = p.prefillMs * 1.0e6;
    if (p.prefillOverS > 0.0 && seconds < p.prefillOverS) ahead *= seconds / p.prefillOverS;
    if (p.switchEveryS > 0.0 && static_cast<int>(seconds / p.switchEveryS) % 2 == 1) ahead += p.switchAheadMs * 1.0e6;
    const auto late = p.stepAtS >= 0.0 && seconds >= p.stepAtS ? p.stepMs * 1.0e6 : 0.0;
    double early {};
    if (seconds >= nextEarly) {
      early = p.earlyMs * 1.0e6;
      nextEarly += p.earlyEveryS;
    }
    double jitterNs = unit(random) * 0.5e6;
    if (p.jitterToMs >= 0.0) {
      const auto width = (0.5 + (p.jitterToMs - 0.5) * std::min(1.0, seconds / p.seconds)) * 1.0e6;
      jitterNs = (unit(random) - 0.5) * width;
    }
    return static_cast<std::int64_t>(static_cast<double>(t0) + playout - ahead + late - early +
                                     static_cast<double>(index - chunkFirst) * 0.1e6 + jitterNs);
  };

  HostResult result;
  std::uint64_t index {};
  auto producerTime = callTime(0);
  std::int64_t pollTime = t0;
  const auto endTime = t0 + static_cast<std::int64_t>(p.seconds * 1.0e9);
  das::sync::StreamReceiver::Block out;
  bool emitted {};
  std::uint64_t previousTs {};
  std::uint32_t previousFrames {};
  auto lastEpoch = sender.epoch();
  double nextSample = 1.0;
  while (pollTime < endTime) {
    if (producerTime <= pollTime) {
      if (static_cast<double>(producerTime - t0) / 1.0e9 >= nextJump) {
        const auto frames = static_cast<std::size_t>(p.jumpMs * 1.0e-3 * p.rate);
        for (std::size_t done = 0; done < frames; done += p.block)
          sender.process(silence.data(), silence.data(), p.block, producerTime);
        nextJump += p.jumpEveryS;
      }
      sender.process(silence.data(), silence.data(), p.block, producerTime);
      if (sender.epoch() != lastEpoch) {
        ++result.epochs;
        lastEpoch = sender.epoch();
      }
      producerTime = callTime(++index);
      continue;
    }
    while (receiver.poll(pollTime, out)) {
      if (!emitted) result.initialDelayMs = static_cast<double>(receiver.delayNs()) / 1.0e6;
      if (emitted && out.timestampNs != previousTs + previousFrames * 1'000'000'000ULL / 48000ULL) {
        ++result.breaks;
        if (static_cast<double>(pollTime - t0) / 1.0e9 > 30.0) ++result.breaksAfter30s;
      }
      previousTs = out.timestampNs;
      previousFrames = out.frames;
      emitted = true;
    }
    const auto seconds = static_cast<double>(pollTime - t0) / 1.0e9;
    if (seconds >= nextSample) {
      nextSample += 1.0;
      if (sender.clockLocked()) {
        result.maxShownPpm = std::max(result.maxShownPpm, std::abs(sender.driftPpm()));
        if (seconds >= settleSeconds)
          result.worstPpmError = std::max(result.worstPpmError, std::abs(sender.driftPpm() - p.ppm));
      } else if (seconds >= settleSeconds) {
        ++result.unlockedSamples;
      }
    }
    pollTime += 1'000'000;
  }
  result.finalPpm = sender.driftPpm();
  result.finalLocked = sender.clockLocked();
  result.finalDelayMs = static_cast<double>(receiver.delayNs()) / 1.0e6;
  return result;
}

void testHostPatterns() {
  struct Case {
    HostPattern pattern;
    double maxPpmError;     // after settleSeconds, against the true drift
    int maxBreaksAfter30s;  // chunked hosts may need 10 ms more delay at any time
    int maxEpochs;          // the start counts as one
    double settleSeconds {20.0};
    double maxDelayGrowthMs {30.0};
    int maxUnlockedSeconds {2};
    bool mustLock {true};
  };
  using P = HostPattern;
  const Case cases[] = {
      {P {"real-time callbacks"}, 3.0, 0, 1},
      {P {"chunks 4 x 256", 48000.0, 256, -15.0, 4}, 3.0, 1, 1},
      {P {"chunks 6 x 512 @48k", 48000.0, 512, -15.0, 6}, 5.0, 1, 1, 30.0},
      {P {"chunks 8 x 512 @48k", 48000.0, 512, -15.0, 8}, 5.0, 1, 1, 30.0},
      {P {"chunks 6 x 512 @44.1k", 44100.0, 512, -15.0, 6}, 5.0, 1, 1, 30.0},
      {P {"chunks 8 x 512 @44.1k", 44100.0, 512, -15.0, 8}, 5.0, 1, 1, 30.0},
      {P {"chunks 6 x 512, -300 ppm", 48000.0, 512, -300.0, 6}, 5.0, 1, 1, 30.0},
      {P {"chunks 6 x 512, +300 ppm", 48000.0, 512, 300.0, 6}, 5.0, 1, 1, 30.0},
      {P {"chunks 6 x 1024 @96k", 96000.0, 1024, -15.0, 6}, 5.0, 1, 1, 30.0},
      {P {"chunks 8 x 1024 @96k", 96000.0, 1024, -15.0, 8}, 5.0, 1, 1, 30.0},
      {P {"chunks 4 x 2048 @48k, +300 ppm", 48000.0, 2048, 300.0, 4}, 10.0, 1, 1, 60.0},
      {P {"chunks 4 x 2048 @44.1k, +300 ppm", 44100.0, 2048, 300.0, 4}, 10.0, 1, 1, 60.0},
      {P {"chunks 8 x 1024 @48k", 48000.0, 1024, -15.0, 8}, 10.0, 1, 1, 60.0, 100.0},
      // Chunks spanning more than the 150 ms reset threshold.
      {P {"chunks 8 x 1024 @44.1k", 44100.0, 1024, -15.0, 8}, 10.0, 1, 2, 60.0, 120.0},
      // 213 ms chunks: accurate, but the slope's error bar crosses the lock
      // limit now and then (the display shows "locking" meanwhile).
      {P {"chunks 5 x 2048 @48k", 48000.0, 2048, -15.0, 5}, 15.0, 1, 2, 60.0, 150.0, 30},
      {P {"pre-fill 40 ms at once", 48000.0, 256, -15.0, 4, 40.0}, 3.0, 1, 1},
      {P {"pre-fill 45 ms over 3 s", 44100.0, 512, 120.0, 4, 45.0, 3.0}, 10.0, 1, 1},
      {P {"pre-fill 100 ms over 10 s", 48000.0, 256, -15.0, 4, 100.0, 10.0}, 10.0, 1, 3, 30.0},
      // Re-rendered audio really is a discontinuity: about one break per jump.
      {P {"re-render 20 ms every 10 s", 48000.0, 256, -15.0, 4, 0.0, 0.0, 20.0, 10.0}, 5.0, 20, 1},
      // Known limit: steps every 3 s leave little undisturbed data; the
      // estimate wanders up to about 24 ppm off (the audio breaks at every
      // step anyway).
      {P {"re-render 20 ms every 3 s", 48000.0, 256, -15.0, 4, 0.0, 0.0, 20.0, 3.0}, 25.0, 62, 1},
      {P {"re-render 40 ms every 5 s", 48000.0, 256, -15.0, 4, 0.0, 0.0, 40.0, 5.0}, 10.0, 36, 1},
      // Known limit, as the one above: up to about 26 ppm off.
      {P {"chunks 8 x 1024 + re-render 20 ms / 3 s", 48000.0, 1024, -15.0, 8, 0.0, 0.0, 20.0, 3.0},
       30.0, 62, 2, 60.0, 150.0, 1000, false},
      // The host falls back (calls later) and stays there.
      {P {"step 100 ms later at 40 s", 48000.0, 256, -15.0, 4, 0.0, 0.0, 0.0, 0.0, 40.0, 100.0},
       5.0, 5, 1, 20.0, 130.0},
      // A step keeps the delay that covered it (harmless: timestamps carry
      // the delay, so placement does not change).
      {P {"step 40 ms later at 40 s", 48000.0, 256, -15.0, 4, 0.0, 0.0, 0.0, 0.0, 40.0, 40.0},
       5.0, 3, 1, 20.0, 60.0},
      {P {"step 40 ms earlier at 40 s", 48000.0, 256, -15.0, 4, 0.0, 0.0, 0.0, 0.0, 40.0, -40.0},
       5.0, 3, 1},
      {P {"step 1.9 ms later at 40 s", 48000.0, 256, -15.0, 1, 0.0, 0.0, 0.0, 0.0, 40.0, 1.9},
       10.0, 1, 1},
  };
  // Second review. Irregular chunk sizes reproduce the Cubase symptom with
  // the first version (+2004 ppm, a re-anchor every 0.1 s).
  auto irregular = [](const char* name, std::size_t block, int low, int high, double ppm) {
    P p {name, 48000.0, block, ppm, low};
    p.chunkMax = high;
    p.seconds = 180.0;
    return p;
  };
  auto earlyCalls = [](const char* name, double everyS, double ms) {
    P p {name, 48000.0, 512, -15.0, 6};
    p.earlyEveryS = everyS;
    p.earlyMs = ms;
    p.seconds = 180.0;
    return p;
  };
  auto switching = [](const char* name, double everyS, double aheadMs) {
    P p {name, 48000.0, 256, -15.0, 4};
    p.switchEveryS = everyS;
    p.switchAheadMs = aheadMs;
    p.seconds = 150.0;
    return p;
  };
  auto prefillDrift = [](const char* name, double ppm) {
    return P {name, 48000.0, 256, ppm, 4, 100.0, 10.0};
  };
  P chunkChange {"chunks 8 -> 2 x 512 at 60 s", 48000.0, 512, -15.0, 8};
  chunkChange.chunkChangeAtS = 60.0;
  chunkChange.chunkAfter = 2;
  P widening {"jitter both ways 0.5 -> 2 ms", 48000.0, 256, 25.0, 1};
  widening.jitterToMs = 2.0;
  widening.seconds = 180.0;
  const Case reviewCases[] = {
      {irregular("irregular chunks 2..6 x 256", 256, 2, 6, -15.0), 10.0, 1, 1, 60.0, 30.0, 10},
      {irregular("irregular chunks 1..8 x 512", 512, 1, 8, -15.0), 10.0, 1, 1, 90.0, 30.0, 30},
      {irregular("irregular chunks 1..16 x 512", 512, 1, 16, -15.0), 15.0, 1, 1, 120.0, 30.0, 60},
      {irregular("irregular chunks 1..8 x 512, +80 ppm", 512, 1, 8, 80.0), 10.0, 1, 1, 90.0, 30.0, 30},
      {earlyCalls("one call 50 ms early every 10 s", 10.0, 50.0), 5.0, 1, 1, 30.0},
      {earlyCalls("one call 20 ms early every 10 s", 10.0, 20.0), 5.0, 1, 1, 30.0},
      {earlyCalls("one call 5 ms early every 2 s", 2.0, 5.0), 5.0, 1, 1, 30.0},
      {prefillDrift("pre-fill 100 ms over 10 s, -80 ppm", -80.0), 10.0, 1, 1, 30.0},
      {prefillDrift("pre-fill 100 ms over 10 s, +60 ppm", 60.0), 10.0, 1, 1, 30.0},
      // A path switch moves the host's timing: about one break each.
      {switching("switch real time / 40 ms ahead every 7 s", 7.0, 40.0), 10.0, 40, 1, 30.0, 60.0, 20},
      {switching("switch real time / 40 ms ahead every 20 s", 20.0, 40.0), 10.0, 14, 1, 30.0, 60.0},
      {chunkChange, 5.0, 3, 1, 30.0, 120.0},
      {widening, 25.0, 0, 1, 30.0},
  };

  std::vector<Case> all(std::begin(cases), std::end(cases));
  all.insert(all.end(), std::begin(reviewCases), std::end(reviewCases));
  for (const auto& entry : all) {
    const auto result = simulateHost(entry.pattern, entry.settleSeconds);
    std::printf("[host: %s] drift %.1f ppm (true %.0f, %s), worst after %.0f s %.1f, unlocked %d s, "
                "shown max %.0f, breaks %d (after 30 s %d), epochs %d, delay %.0f->%.0f ms\n",
                entry.pattern.name, result.finalPpm, entry.pattern.ppm,
                result.finalLocked ? "locked" : "NOT LOCKED", entry.settleSeconds, result.worstPpmError,
                result.unlockedSamples, result.maxShownPpm, result.breaks, result.breaksAfter30s,
                result.epochs, result.initialDelayMs, result.finalDelayMs);
    const std::string name = std::string("host ") + entry.pattern.name;
    check(result.finalLocked || !entry.mustLock, name + ": the clock must end up locked");
    check(std::abs(result.finalPpm - entry.pattern.ppm) <= entry.maxPpmError, name + ": final drift estimate off");
    check(result.worstPpmError <= entry.maxPpmError, name + ": drift estimate pulled by host timing");
    check(result.unlockedSamples <= entry.maxUnlockedSeconds, name + ": the clock keeps losing its lock");
    check(result.maxShownPpm <= std::abs(entry.pattern.ppm) + 150.0,
          name + ": a wrong value was shown as locked");
    check(result.breaksAfter30s <= entry.maxBreaksAfter30s, name + ": OBS output keeps breaking");
    check(result.epochs <= entry.maxEpochs, name + ": host timing restarted the clock");
    // A chunked host needs its whole chunk of delay; anything beyond stayed grown.
    const auto chunkMs = static_cast<double>(entry.pattern.chunk) * static_cast<double>(entry.pattern.block) /
                         entry.pattern.rate * 1000.0;
    check(result.finalDelayMs <= result.initialDelayMs + std::max(entry.maxDelayGrowthMs, chunkMs + 30.0),
          name + ": the receiver delay stayed grown");
  }
}

} // namespace

int main() {
  testInterpolator();
  testEstimator();
  testDriftAbsorption("44.1k +250ppm", 44100.0, 250.0, 512, 3.0, 600.0);
  testDriftAbsorption("48k -300ppm", 48000.0, -300.0, 256, 2.0, 300.0);
  testOffsetChanges();
  testPause();
  testClockReadFailure();
  testOffsetCeiling();
  testBursts();
  testHostPatterns();
  if (failures == 0) std::printf("all sync tests passed\n");
  return failures == 0 ? 0 : 1;
}
