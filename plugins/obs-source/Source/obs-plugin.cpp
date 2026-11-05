// SPDX-License-Identifier: GPL-3.0-or-later
// Modified for DAS Kai (2026-11-04): reads through StreamReceiver
// (clock-drift absorption and DAW-time timestamps). See MODIFICATIONS.md.
#include <das/sync/StreamReceiver.h>
#include <das/transport/NamedSharedMemory.h>
#include <das/transport/RingBuffer.h>

#include <obs-module.h>
#include <util/platform.h>
#include <util/threading.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("das-kai-obs", "en-US")
MODULE_EXPORT const char* obs_module_description(void) {
  return "Receives DAW audio from the DAS Kai plug-in with clock-drift absorption and "
         "latency compensation";
}

namespace {
using das::sync::StreamReceiver;

std::wstring mappingName() {
  std::wstring name(das::transport::kObsAudioMappingName);
  // Tests run against a private namespace, matching the plug-in side.
  if (const auto* suffix = std::getenv("KINU_STREAM_TEST_NAMESPACE");
      suffix != nullptr && *suffix != '\0') {
    name.push_back(L'.');
    while (*suffix != '\0') name.push_back(static_cast<unsigned char>(*suffix++));
  }
  return name;
}

// One reader per OBS process: the ring has a single consumer, so several
// DAS Kai sources share one receiver and get the same audio and timestamps.
class SharedReceiver final {
public:
  void add(obs_source_t* source) {
    const std::lock_guard lifecycle(lifecycleMutex_);
    {
      const std::lock_guard lock(sourcesMutex_);
      sources_.push_back(source);
    }
    if (!worker_.joinable()) start();
  }

  void remove(obs_source_t* source) {
    const std::lock_guard lifecycle(lifecycleMutex_);
    bool empty {};
    {
      const std::lock_guard lock(sourcesMutex_);
      std::erase(sources_, source);
      empty = sources_.empty();
    }
    if (empty) stop();
  }

  ~SharedReceiver() { stop(); }

private:
  void start() {
    stopping_.store(false, std::memory_order_release);
    worker_ = std::thread([this] { run(); });
  }

  void stop() {
    stopping_.store(true, std::memory_order_release);
    if (worker_.joinable()) worker_.join();
  }

  bool attach() {
    const auto bytes = das::transport::requiredBytes(das::transport::kAudioCapacityFrames,
                                                      das::transport::kAudioChannels);
    memory_ = das::transport::NamedSharedMemory::create(mappingName(), bytes);
    if (!memory_.isOpen()) return false;
    if (!memory_.alreadyExisted())
      das::transport::RingBuffer::initialize(memory_.storage(),
                                             das::transport::kAudioCapacityFrames,
                                             das::transport::kAudioChannels,
                                             das::transport::kAudioSampleRate);
    ring_ = std::make_unique<das::transport::RingBuffer>(memory_.storage());
    if (!ring_->isCompatible()) {
      ring_.reset();
      memory_ = {};
      return false;
    }
    receiver_ = std::make_unique<StreamReceiver>(*ring_);
    receiver_->setEventHandler(&SharedReceiver::onEvent, this);
    return true;
  }

  // Receiver events go to the OBS log (Help > Log Files), so a problem in a
  // real session can be read back afterwards. At most kEventsPerMinute lines
  // a minute; the rest is counted in the next status line.
  static void onEvent(void* context, const StreamReceiver::Event event, const double valueMs) {
    static_cast<SharedReceiver*>(context)->logEvent(event, valueMs);
  }

  void logEvent(const StreamReceiver::Event event, const double valueMs) {
    using Event = StreamReceiver::Event;
    if (eventsThisMinute_ >= kEventsPerMinute) {
      ++suppressedEvents_;
      return;
    }
    ++eventsThisMinute_;
    const auto drift = receiver_->producerDriftPpm();
    switch (event) {
    case Event::started:
      blog(LOG_INFO, "[DAS Kai] receiving: delay %.0f ms, DAW clock %+.1f ppm", valueMs, drift);
      break;
    case Event::stopped:
      blog(LOG_INFO, "[DAS Kai] the DAW stopped sending");
      break;
    case Event::producerRestarted:
      blog(LOG_INFO, "[DAS Kai] the DAW restarted its clock (engine restart, pause or export)");
      break;
    case Event::timingStep:
      blog(LOG_INFO, "[DAS Kai] the DAW's timing stepped by %+.1f ms; output restarted (DAW clock %+.1f ppm)",
           valueMs, drift);
      break;
    case Event::lateAudio:
      blog(LOG_INFO, "[DAS Kai] DAW audio arrived late; waiting longer: delay %.0f ms", valueMs);
      break;
    case Event::starved:
      blog(LOG_WARNING, "[DAS Kai] the receiver thread ran %.0f ms late", valueMs);
      break;
    case Event::readFailed:
      blog(LOG_WARNING, "[DAS Kai] audio was overwritten before it could be read");
      break;
    }
  }

  void logStatus(const std::int64_t nowNs) {
    if (nowNs - minuteStartNs_ < kStatusIntervalNs) return;
    minuteStartNs_ = nowNs;
    if (receiver_->state() != StreamReceiver::State::running) {
      // Not receiving: only report what the rate limit kept out of the log.
      if (suppressedEvents_ > 0)
        blog(LOG_INFO, "[DAS Kai] %llu events were not logged in the last minute",
             static_cast<unsigned long long>(suppressedEvents_));
    } else {
      blog(LOG_INFO,
           "[DAS Kai] status: DAW clock %+.1f ppm, delay %.0f ms, shift %.1f ms, resyncs %llu, "
           "late audio %llu, unlogged events %llu",
           receiver_->producerDriftPpm(), static_cast<double>(receiver_->delayNs()) / 1.0e6,
           static_cast<double>(receiver_->appliedOffsetNs()) / 1.0e6,
           static_cast<unsigned long long>(receiver_->resyncs()),
           static_cast<unsigned long long>(receiver_->underruns()),
           static_cast<unsigned long long>(suppressedEvents_));
    }
    eventsThisMinute_ = 0;
    suppressedEvents_ = 0;
  }

  void detach() {
    receiver_.reset();
    ring_.reset();
    memory_ = {};
  }

  void run() {
    os_set_thread_name("das-kai-receiver");
    StreamReceiver::Block block;
    std::array<float, StreamReceiver::kMaximumPacketFrames> left {};
    std::array<float, StreamReceiver::kMaximumPacketFrames> right {};
    while (!stopping_.load(std::memory_order_acquire)) {
      if (!receiver_ && !attach()) {
        // An older or foreign layout occupies the name; retry occasionally.
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        continue;
      }
      const auto now = static_cast<std::int64_t>(os_gettime_ns());
      logStatus(now);
      if (receiver_->poll(now, block)) {
        for (std::uint32_t frame = 0; frame < block.frames; ++frame) {
          left[frame] = block.samples[frame * 2];
          right[frame] = block.samples[frame * 2 + 1];
        }
        obs_source_audio audio {};
        audio.data[0] = reinterpret_cast<std::uint8_t*>(left.data());
        audio.data[1] = reinterpret_cast<std::uint8_t*>(right.data());
        audio.frames = block.frames;
        audio.speakers = SPEAKERS_STEREO;
        audio.samples_per_sec = das::transport::kAudioSampleRate;
        audio.format = AUDIO_FORMAT_FLOAT_PLANAR;
        audio.timestamp = block.timestampNs;
        // destroy() waits in remove() for this loop, so no source is freed mid-output.
        const std::lock_guard lock(sourcesMutex_);
        for (auto* source : sources_) obs_source_output_audio(source, &audio);
        continue; // another block may already be due after a scheduling hiccup
      }
      const auto wake = receiver_->nextWakeNs(now);
      const auto sleepNs = std::clamp<std::int64_t>(wake - now, 500'000, 5'000'000);
      std::this_thread::sleep_for(std::chrono::nanoseconds(sleepNs));
    }
    detach();
  }

  std::mutex lifecycleMutex_;
  std::mutex sourcesMutex_;
  std::vector<obs_source_t*> sources_;
  das::transport::NamedSharedMemory memory_;
  std::unique_ptr<das::transport::RingBuffer> ring_;
  std::unique_ptr<StreamReceiver> receiver_;
  std::atomic<bool> stopping_ {};
  std::thread worker_;
  // Log rate limiting; touched only by the worker thread.
  static constexpr int kEventsPerMinute = 20;
  static constexpr std::int64_t kStatusIntervalNs = 60'000'000'000;
  std::int64_t minuteStartNs_ {};
  int eventsThisMinute_ {};
  std::uint64_t suppressedEvents_ {};
};

SharedReceiver& receiver() {
  static SharedReceiver instance;
  return instance;
}

struct DasKaiSource final {
  explicit DasKaiSource(obs_source_t* owner) : source(owner) { receiver().add(source); }
  ~DasKaiSource() { receiver().remove(source); }

  obs_source_t* source {};
};

const char* sourceName(void*) { return obs_module_text("DasKaiSource"); }

void* createSource(obs_data_t*, obs_source_t* source) {
  try {
    return new DasKaiSource(source);
  } catch (...) {
    return nullptr;
  }
}

void destroySource(void* data) { delete static_cast<DasKaiSource*>(data); }

obs_properties_t* sourceProperties(void*) {
  auto* properties = obs_properties_create();
  obs_properties_add_text(properties, "setup_help", obs_module_text("SetupHelp"), OBS_TEXT_INFO);
  return properties;
}

obs_source_info sourceInfo {
    .id = "kinu_stream_audio_source",
    .type = OBS_SOURCE_TYPE_INPUT,
    .output_flags = OBS_SOURCE_AUDIO | OBS_SOURCE_DO_NOT_DUPLICATE | OBS_SOURCE_CAP_DONT_SHOW_PROPERTIES,
    .get_name = sourceName,
    .create = createSource,
    .destroy = destroySource,
    .get_properties = sourceProperties,
};
} // namespace

bool obs_module_load(void) {
  obs_register_source(&sourceInfo);
  return true;
}
