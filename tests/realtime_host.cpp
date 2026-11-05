// Real-time host: runs the real DAS Kai VST3 binary from an audio device's own
// callback (CoreAudio), the way a DAW's real-time path does. With --guard N it
// instead processes ahead of the device on a worker thread, N blocks at a time
// (the way Cubase's ASIO-Guard path behaves, as far as we can emulate it).
// Pair it with das_kai_live_monitor (OBS must be closed). The device output is
// always silent; the plug-in gets a quiet 1 kHz tone.
//
//   das_kai_realtime_host --list
//   das_kai_realtime_host <DAS Kai.vst3> <seconds> [--device <name>] [--buffer <frames>]
//                         [--guard <blocks>]
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr double kPi = 3.14159265358979323846;

juce::AudioIODeviceType* coreAudio(juce::OwnedArray<juce::AudioIODeviceType>& types) {
  juce::AudioDeviceManager manager;
  manager.createAudioDeviceTypes(types);
  for (auto* type : types)
    if (type->getTypeName() == "CoreAudio") {
      type->scanForDevices();
      return type;
    }
  return nullptr;
}

class Host final : public juce::AudioIODeviceCallback {
public:
  Host(juce::AudioPluginInstance& plugin, const int blockSize, const int guardBlocks)
      : plugin_(plugin), blockSize_(blockSize), guardBlocks_(guardBlocks) {}

  void audioDeviceAboutToStart(juce::AudioIODevice* device) override {
    rate_ = device->getCurrentSampleRate();
    plugin_.setPlayConfigDetails(2, 2, rate_, blockSize_);
    plugin_.prepareToPlay(rate_, blockSize_);
    buffer_.setSize(2, blockSize_);
    if (guardBlocks_ > 0) {
      stop_ = false;
      worker_ = std::thread([this] { guardLoop(); });
    }
  }

  void audioDeviceStopped() override {
    if (worker_.joinable()) {
      {
        const std::lock_guard lock(mutex_);
        stop_ = true;
      }
      wake_.notify_all();
      worker_.join();
    }
    plugin_.releaseResources();
  }

  void audioDeviceIOCallbackWithContext(const float* const*, int, float* const* outputs,
                                        const int outputChannels, const int frames,
                                        const juce::AudioIODeviceCallbackContext&) override {
    ++callbacks_;
    if (guardBlocks_ == 0) {
      // Real-time path: process exactly what the device asks for, now.
      for (int done = 0; done < frames; done += blockSize_) process(std::min(blockSize_, frames - done));
    } else {
      {
        const std::lock_guard lock(mutex_);
        consumed_ += frames;
      }
      wake_.notify_one();
    }
    for (int channel = 0; channel < outputChannels; ++channel)
      if (outputs[channel] != nullptr) juce::FloatVectorOperations::clear(outputs[channel], frames);
  }

  [[nodiscard]] long long callbacks() const noexcept { return callbacks_.load(); }
  [[nodiscard]] long long blocks() const noexcept { return blocks_.load(); }

private:
  void process(const int frames) {
    buffer_.setSize(2, frames, false, false, true);
    for (int i = 0; i < frames; ++i) {
      const auto value = static_cast<float>(0.01 * std::sin(phase_));
      phase_ += 2.0 * kPi * 1000.0 / rate_;
      buffer_.setSample(0, i, value);
      buffer_.setSample(1, i, value);
    }
    phase_ = std::fmod(phase_, 2.0 * kPi);
    plugin_.processBlock(buffer_, midi_);
    ++blocks_;
  }

  // Pre-fetch: keep guardBlocks_ blocks processed ahead of the device; refill a
  // whole chunk at once whenever the device has consumed one.
  void guardLoop() {
    long long produced {};
    for (int i = 0; i < guardBlocks_; ++i) process(blockSize_);
    produced += guardBlocks_;
    std::unique_lock lock(mutex_);
    while (!stop_) {
      wake_.wait(lock, [this, produced] {
        return stop_ || consumed_ / blockSize_ + guardBlocks_ >= produced + guardBlocks_;
      });
      if (stop_) break;
      lock.unlock();
      for (int i = 0; i < guardBlocks_; ++i) process(blockSize_);
      produced += guardBlocks_;
      lock.lock();
    }
  }

  juce::AudioPluginInstance& plugin_;
  const int blockSize_;
  const int guardBlocks_;
  double rate_ {48000.0};
  double phase_ {};
  juce::AudioBuffer<float> buffer_;
  juce::MidiBuffer midi_;
  std::thread worker_;
  std::mutex mutex_;
  std::condition_variable wake_;
  long long consumed_ {};
  bool stop_ {};
  std::atomic<long long> callbacks_ {};
  std::atomic<long long> blocks_ {};
};
} // namespace

int main(const int argc, char** argv) {
  juce::ScopedJuceInitialiser_GUI juce;
  juce::OwnedArray<juce::AudioIODeviceType> types;
  auto* type = coreAudio(types);
  if (type == nullptr) {
    std::fprintf(stderr, "no CoreAudio\n");
    return 1;
  }
  if (argc >= 2 && std::string(argv[1]) == "--list") {
    for (const auto& name : type->getDeviceNames(false)) std::printf("output: %s\n", name.toRawUTF8());
    return 0;
  }
  if (argc < 3) {
    std::fprintf(stderr, "usage: das_kai_realtime_host <DAS Kai.vst3> <seconds> [--device n] [--buffer n] [--guard n]\n");
    return 1;
  }
  const auto seconds = std::atof(argv[2]);
  juce::String deviceName = type->getDeviceNames(false)[type->getDefaultDeviceIndex(false)];
  int buffer = 256;
  int guard = 0;
  for (int i = 3; i + 1 < argc; i += 2) {
    const std::string option = argv[i];
    if (option == "--device") deviceName = juce::String::fromUTF8(argv[i + 1]);
    if (option == "--buffer") buffer = std::atoi(argv[i + 1]);
    if (option == "--guard") guard = std::atoi(argv[i + 1]);
  }

  juce::VST3PluginFormat format;
  juce::OwnedArray<juce::PluginDescription> descriptions;
  format.findAllTypesForFile(descriptions, juce::File(juce::String::fromUTF8(argv[1])).getFullPathName());
  if (descriptions.size() != 1) {
    std::fprintf(stderr, "DAS Kai VST3 not found\n");
    return 2;
  }

  std::unique_ptr<juce::AudioIODevice> device(type->createDevice(deviceName, {}));
  if (!device) {
    std::fprintf(stderr, "cannot open %s\n", deviceName.toRawUTF8());
    return 3;
  }
  // Keep the device's current sample rate: changing it would disturb other
  // apps using the same interface.
  auto rate = device->getCurrentSampleRate();
  if (!(rate > 0.0)) rate = 48000.0;

  juce::String error;
  auto plugin = format.createInstanceFromDescription(*descriptions[0], rate, buffer, error);
  if (!plugin) {
    std::fprintf(stderr, "cannot instantiate: %s\n", error.toRawUTF8());
    return 4;
  }
  juce::BigInteger outputs;
  outputs.setRange(0, 2, true);
  error = device->open({}, outputs, rate, buffer);
  if (error.isNotEmpty()) {
    std::fprintf(stderr, "open failed: %s\n", error.toRawUTF8());
    return 5;
  }
  Host host(*plugin, buffer, guard);
  device->start(&host);
  std::printf("device %s, %.0f Hz, buffer %d (device %d), %s\n", deviceName.toRawUTF8(),
              device->getCurrentSampleRate(), buffer, device->getCurrentBufferSizeSamples(),
              guard > 0 ? ("pre-fetch " + std::to_string(guard) + " blocks").c_str() : "real-time path");
  std::fflush(stdout);
  std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(seconds * 1000.0)));
  device->stop();
  device->close();
  std::printf("callbacks %lld, plug-in blocks %lld\n", host.callbacks(), host.blocks());
  return 0;
}
