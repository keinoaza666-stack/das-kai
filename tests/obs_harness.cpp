// SPDX-License-Identifier: GPL-3.0-or-later
// DAS Kai: runs the real libobs audio engine headless (from the installed
// OBS.app), loads the built DAS Kai OBS plug-in, feeds it from a
// StreamSender whose "DAW clock" runs off by a chosen ppm, and analyses OBS's
// final audio mix: where OBS places the audio in time, whether the drift
// accumulates, whether the audio is continuous, and what OBS logs about
// buffering. Touches no OBS profile or scene: libobs starts without config.
//
// Usage: das_kai_obs_harness <das-kai-obs.plugin> [seconds] [ppm] [offset-at-s]
#include <das/audio/StreamSender.h>
#include <das/transport/NamedSharedMemory.h>

#include <obs.h>
#include <util/base.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <numbers>
#include <string>
#include <thread>
#include <time.h>
#include <unistd.h>
#include <vector>

namespace {
constexpr double kPi = std::numbers::pi;
std::int64_t nowNs() { return static_cast<std::int64_t>(clock_gettime_nsec_np(CLOCK_UPTIME_RAW)); }

std::mutex logMutex;
std::vector<std::string> interestingLog;

void logHandler(int level, const char* format, va_list args, void*) {
  char line[4096];
  vsnprintf(line, sizeof(line), format, args);
  const std::string text(line);
  if (level <= LOG_WARNING || text.find("buffering") != std::string::npos ||
      text.find("das-kai") != std::string::npos || text.find("DAS Kai") != std::string::npos ||
      text.find("timestamp") != std::string::npos) {
    const std::lock_guard lock(logMutex);
    interestingLog.push_back(text);
  }
}

struct Analysis {
  std::int64_t originNs {};
  std::mutex mutex;
  std::vector<std::pair<double, double>> errors; // (seconds since origin, timing error ms)
  double worstResidual {};
  double worstResidualAt {};
  std::uint64_t callbacks {};
  std::uint64_t silentCallbacks {};
  std::uint64_t timestampGaps {};
  std::uint64_t lastTs {};
  float lastTwo[2] {};
  bool haveLast {};
};

void mixCallback(void* param, size_t, struct audio_data* data) {
  auto& analysis = *static_cast<Analysis*>(param);
  const std::lock_guard lock(analysis.mutex);
  ++analysis.callbacks;
  const auto* left = reinterpret_cast<const float*>(data->data[0]);
  const auto* right = reinterpret_cast<const float*>(data->data[1]);
  const auto frames = data->frames;
  double peak {};
  for (std::uint32_t i = 0; i < frames; ++i) peak = std::max(peak, static_cast<double>(std::abs(right[i])));
  const auto expectedTs = analysis.lastTs + static_cast<std::uint64_t>(frames * 1.0e9 / 48000.0 + 0.5);
  const auto continuous = analysis.haveLast && std::llabs(static_cast<std::int64_t>(data->timestamp) -
                                                          static_cast<std::int64_t>(expectedTs)) < 1000;
  if (analysis.haveLast && !continuous) ++analysis.timestampGaps;
  analysis.lastTs = data->timestamp;
  if (peak < 0.25) {
    // Silence (before the stream starts, or a deliberate gap): no timing to read.
    ++analysis.silentCallbacks;
    analysis.haveLast = false;
    return;
  }
  const auto seconds = static_cast<double>(static_cast<std::int64_t>(data->timestamp) - analysis.originNs) / 1.0e9;

  double ss {}, sc {}, cc {}, ys {}, yc {};
  for (std::uint32_t i = 0; i < frames; ++i) {
    const auto t = seconds + i / 48000.0;
    const auto s = std::sin(2.0 * kPi * 4.0 * t);
    const auto c = std::cos(2.0 * kPi * 4.0 * t);
    const auto y = static_cast<double>(left[i]);
    ss += s * s; sc += s * c; cc += c * c; ys += y * s; yc += y * c;
  }
  const auto determinant = ss * cc - sc * sc;
  const auto a = (ys * cc - yc * sc) / determinant;
  const auto b = (yc * ss - ys * sc) / determinant;
  analysis.errors.emplace_back(seconds, std::atan2(-b, a) / (2.0 * kPi * 4.0) * 1000.0);

  const auto recurrence = 2.0 * std::cos(2.0 * kPi * 1000.0 / 48000.0);
  for (std::uint32_t i = 0; i < frames; ++i) {
    const auto y = right[i];
    if (analysis.haveLast || i >= 2) {
      const auto residual = std::abs(static_cast<double>(y) - recurrence * analysis.lastTwo[1] + analysis.lastTwo[0]);
      if (residual > analysis.worstResidual) {
        analysis.worstResidual = residual;
        analysis.worstResidualAt = seconds;
      }
    }
    analysis.lastTwo[0] = analysis.lastTwo[1];
    analysis.lastTwo[1] = y;
  }
  analysis.haveLast = true;
}

double meanError(const Analysis& analysis, const double from, const double to, double* worst) {
  double sum {};
  std::size_t count {};
  for (const auto& [at, error] : analysis.errors)
    if (at >= from && at < to) { sum += error; ++count; }
  const auto mean = count != 0 ? sum / static_cast<double>(count) : NAN;
  if (worst != nullptr) {
    *worst = 0.0;
    for (const auto& [at, error] : analysis.errors)
      if (at >= from && at < to) *worst = std::max(*worst, std::abs(error - mean));
  }
  return mean;
}
} // namespace

int main(const int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <das-kai-obs.plugin> [seconds] [ppm] [offset-at-s]\n", argv[0]);
    return 1;
  }
  const std::string bundle = argv[1];
  const auto seconds = argc > 2 ? std::atof(argv[2]) : 120.0;
  const auto ppm = argc > 3 ? std::atof(argv[3]) : 1000.0;
  const auto offsetAt = argc > 4 ? std::atof(argv[4]) : seconds * 0.75;
  const auto suffix = "obs-harness-" + std::to_string(getpid());
  setenv("KINU_STREAM_TEST_NAMESPACE", suffix.c_str(), 1);

  base_set_log_handler(logHandler, nullptr);
  if (!obs_startup("en-US", nullptr, nullptr)) return 2;
  obs_video_info video {};
  video.graphics_module = "/Applications/OBS.app/Contents/Frameworks/libobs-opengl.dylib";
  video.fps_num = 30;
  video.fps_den = 1;
  video.base_width = video.output_width = 320;
  video.base_height = video.output_height = 180;
  video.output_format = VIDEO_FORMAT_NV12;
  video.colorspace = VIDEO_CS_709;
  video.range = VIDEO_RANGE_PARTIAL;
  video.scale_type = OBS_SCALE_BICUBIC;
  video.gpu_conversion = true;
  if (const auto result = obs_reset_video(&video); result != OBS_VIDEO_SUCCESS) {
    std::fprintf(stderr, "obs_reset_video failed: %d\n", result);
    return 3;
  }
  obs_audio_info audio {48000, SPEAKERS_STEREO};
  if (!obs_reset_audio(&audio)) return 4;

  const auto binary = bundle + "/Contents/MacOS/das-kai-obs";
  const auto dataPath = bundle + "/Contents/Resources";
  obs_module_t* module {};
  if (const auto result = obs_open_module(&module, binary.c_str(), dataPath.c_str()); result != MODULE_SUCCESS) {
    std::fprintf(stderr, "obs_open_module failed: %d\n", result);
    return 5;
  }
  if (!obs_init_module(module)) return 6;
  obs_source_t* source = obs_source_create("kinu_stream_audio_source", "DAS Kai", nullptr, nullptr);
  if (source == nullptr) return 7;
  obs_set_output_source(0, source);

  Analysis analysis;
  analysis.originNs = nowNs();
  obs_add_raw_audio_callback(0, nullptr, mixCallback, &analysis);

  // "DAW": 44.1 kHz nominal, but its clock produces (1 + ppm) samples per real second.
  std::atomic<bool> stop {};
  std::atomic<bool> offsetApplied {};
  std::thread producer([&] {
    das::audio::StreamSender sender;
    // The OBS plug-in created and initialised the shared ring; attach to it.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const auto bytes = das::transport::requiredBytes(das::transport::kAudioCapacityFrames,
                                                      das::transport::kAudioChannels);
    auto memory = das::transport::NamedSharedMemory::open(
        std::wstring(das::transport::kObsAudioMappingName) + L'.' + std::wstring(suffix.begin(), suffix.end()), bytes);
    das::transport::RingBuffer ring(memory.storage());
    if (!memory.isOpen() || !ring.isCompatible()) {
      std::fprintf(stderr, "producer could not attach to the OBS ring\n");
      return;
    }
    constexpr std::size_t block = 512;
    const auto trueRate = 44100.0 * (1.0 + ppm * 1.0e-6);
    sender.prepare(44100.0, block, &ring);
    std::vector<float> left(block), right(block);
    std::uint64_t sample {};
    const auto origin = analysis.originNs;
    const auto start = nowNs();
    auto firstSample = static_cast<std::uint64_t>((start - origin) / 1.0e9 * trueRate);
    sample = firstSample;
    while (!stop.load()) {
      const auto endTime = origin + static_cast<std::int64_t>((sample + block) / trueRate * 1.0e9);
      while (nowNs() < endTime) std::this_thread::sleep_for(std::chrono::microseconds(200));
      for (std::size_t i = 0; i < block; ++i) {
        const auto t = (sample + i) / trueRate;
        left[i] = static_cast<float>(0.5 * std::sin(2.0 * kPi * 4.0 * t));
        right[i] = static_cast<float>(0.5 * std::sin(2.0 * kPi * 1000.0 * t));
      }
      const auto elapsed = static_cast<double>(nowNs() - origin) / 1.0e9;
      const auto manual = elapsed >= offsetAt ? 45'000'000 : 0;
      if (manual != 0) offsetApplied = true;
      sender.setControl(true, manual);
      sender.process(left.data(), right.data(), block, nowNs());
      sample += block;
    }
    std::printf("sender: drift estimate %.1f ppm (true %.0f), lead %.1f ms\n", sender.driftPpm(), ppm,
                sender.leadNs() / 1.0e6);
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(seconds * 1000.0)));
  stop = true;
  producer.join();
  obs_remove_raw_audio_callback(0, mixCallback, &analysis);
  obs_set_output_source(0, nullptr);
  obs_source_release(source);

  {
    const std::lock_guard lock(analysis.mutex);
    double worstBefore {}, worstAfter {};
    const auto settled = 10.0;
    const auto early = meanError(analysis, settled, settled + 20.0, nullptr);
    const auto late = meanError(analysis, offsetAt - 20.0, offsetAt, nullptr);
    const auto before = meanError(analysis, settled, offsetAt, &worstBefore);
    const auto after = meanError(analysis, offsetAt + 2.0, seconds - 1.0, &worstAfter);
    std::printf("mix callbacks %llu (silent %llu), timestamp gaps %llu\n",
                static_cast<unsigned long long>(analysis.callbacks),
                static_cast<unsigned long long>(analysis.silentCallbacks),
                static_cast<unsigned long long>(analysis.timestampGaps));
    std::printf("timing error (mix time - DAW time): %.3f ms, worst deviation %.3f ms\n", before, worstBefore);
    std::printf("drift over the run: first 20 s %.3f ms, last 20 s before offset %.3f ms\n", early, late);
    std::printf("after +45 ms manual offset: %.3f ms (expected %.3f), worst deviation %.3f ms\n", after,
                before - 45.0, worstAfter);
    std::printf("worst 1 kHz continuity residual %.2e at %.2f s\n", analysis.worstResidual, analysis.worstResidualAt);
  }
  {
    const std::lock_guard lock(logMutex);
    std::printf("libobs log lines of interest: %zu\n", interestingLog.size());
    for (const auto& line : interestingLog) std::printf("  | %s\n", line.c_str());
  }
  obs_shutdown();
  return 0;
}
