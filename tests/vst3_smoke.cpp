// SPDX-License-Identifier: AGPL-3.0-only
// Modified for DAS Kai (2026-11-04): loads the built VST3, drives it in
// real time and receives its audio through StreamReceiver. Runs on macOS and
// Windows (2026-11-05). See MODIFICATIONS.md.
#include <das/sync/StreamReceiver.h>
#include <das/transport/MonotonicClock.h>
#include <das/transport/NamedSharedMemory.h>
#include <das/transport/RingBuffer.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <numbers>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <process.h>
// winmm, declared here so <Windows.h> does not meet the JUCE headers.
extern "C" __declspec(dllimport) unsigned int __stdcall timeBeginPeriod(unsigned int);
#else
#include <unistd.h>
#endif

namespace {
std::int64_t nowNs() { return das::transport::monotonicNowNs(); }

// The plug-in reads this to give its shared memory and lock test-only names.
void setTestNamespace(const std::string& value) {
#if defined(_WIN32)
  _putenv_s("KINU_STREAM_TEST_NAMESPACE", value.c_str());
#else
  setenv("KINU_STREAM_TEST_NAMESPACE", value.c_str(), 1);
#endif
}

unsigned long long processId() {
#if defined(_WIN32)
  return static_cast<unsigned long long>(_getpid());
#else
  return static_cast<unsigned long long>(getpid());
#endif
}

// Hosted VST3 parameters are plain AudioProcessorParameters (normalised 0..1).
juce::AudioProcessorParameter* findParameter(juce::AudioPluginInstance& plugin, const juce::String& name) {
  for (auto* parameter : plugin.getParameters())
    if (parameter->getName(64) == name) return parameter;
  std::cerr << "parameters:";
  for (auto* parameter : plugin.getParameters()) std::cerr << " [" << parameter->getName(64) << "]";
  std::cerr << '\n';
  return nullptr;
}

int fail(const int code, const std::string& message) {
  std::cerr << message << '\n';
  return code;
}
} // namespace

int main(const int argc, char** argv) {
  juce::ScopedJuceInitialiser_GUI juce;
  if (argc != 2) return fail(1, "usage: das_kai_vst3_smoke <DAS Kai.vst3>");
#if defined(_WIN32)
  // The pacing loop below sleeps 0.5 ms; Windows' default 15.6 ms timer would
  // turn that into bursts the test harness made, not the plug-in.
  timeBeginPeriod(1);
#endif

  const auto suffix = std::string("vst3-smoke-") + std::to_string(processId());
  setTestNamespace(suffix);
  const auto mapping = std::wstring(das::transport::kObsAudioMappingName) + L'.' +
                       std::wstring(suffix.begin(), suffix.end());
  const auto bytes = das::transport::requiredBytes(das::transport::kAudioCapacityFrames,
                                                    das::transport::kAudioChannels);
  auto memory = das::transport::NamedSharedMemory::create(mapping, bytes);
  if (!memory.isOpen()) return fail(2, "could not create the test mapping");
  if (!memory.alreadyExisted())
    das::transport::RingBuffer::initialize(memory.storage(), das::transport::kAudioCapacityFrames,
                                           das::transport::kAudioChannels,
                                           das::transport::kAudioSampleRate);
  das::transport::RingBuffer ring(memory.storage());
  if (!ring.isCompatible()) return fail(3, "test ring incompatible");

  juce::VST3PluginFormat format;
  juce::OwnedArray<juce::PluginDescription> descriptions;
  format.findAllTypesForFile(descriptions, juce::File(juce::String::fromUTF8(argv[1])).getFullPathName());
  if (descriptions.size() != 1) return fail(4, "DAS Kai VST3 not found");
  if (descriptions[0]->name != "DAS Kai" || descriptions[0]->manufacturerName != "Keinoaza")
    return fail(4, "unexpected plug-in identity: " + descriptions[0]->name.toStdString() + " / " +
                       descriptions[0]->manufacturerName.toStdString());

  constexpr double rate = 44100.0;
  constexpr int blockSize = 512;
  juce::String error;
  auto plugin = format.createInstanceFromDescription(*descriptions[0], rate, blockSize, error);
  if (!plugin) return fail(5, "cannot instantiate: " + error.toStdString());
  plugin->setPlayConfigDetails(2, 2, rate, blockSize);
  plugin->prepareToPlay(rate, blockSize);

  juce::AudioBuffer<float> audio(2, blockSize);
  juce::MidiBuffer midi;

  // Bypass passes audio through untouched and keeps the stream clock running
  // with silence.
  // The 44.1 -> 48 kHz converter holds back its first ~20 ms, so feed several
  // blocks before expecting output.
  const auto before = ring.writePosition();
  for (int i = 0; i < 10; ++i) {
    audio.clear();
    audio.setSample(0, 0, 0.25F);
    audio.setSample(1, 0, -0.25F);
    plugin->processBlockBypassed(audio, midi);
    if (audio.getSample(0, 0) != 0.25F || audio.getSample(1, 0) != -0.25F)
      return fail(13, "bypass changed the DAW audio");
  }
  const auto afterBypass = ring.writePosition();
  if (afterBypass <= before) return fail(14, "bypass must keep sending (silence)");
  std::vector<float> check(static_cast<std::size_t>(afterBypass - before) * 2);
  if (!ring.readAt(before, static_cast<std::uint32_t>(afterBypass - before), check))
    return fail(14, "cannot read bypass frames");
  for (const auto value : check)
    if (value != 0.0F) return fail(14, "bypass sent non-silent audio");

  // A second instance must not send.
  auto duplicate = format.createInstanceFromDescription(*descriptions[0], rate, blockSize, error);
  if (!duplicate) return fail(7, "cannot instantiate a second instance: " + error.toStdString());
  duplicate->setPlayConfigDetails(2, 2, rate, blockSize);
  duplicate->prepareToPlay(rate, blockSize);
  audio.clear();
  duplicate->processBlock(audio, midi);
  if (ring.writePosition() != afterBypass) return fail(8, "a second instance sent audio");
  duplicate->releaseResources();
  duplicate.reset();

  // Manual offset parameter reaches the transport controls.
  auto* manual = findParameter(*plugin, "Manual Offset");
  auto* automatic = findParameter(*plugin, "Auto Compensation");
  if (manual == nullptr || automatic == nullptr) return fail(15, "sync parameters missing");
  // Range -100..100 ms (PluginProcessor.h); 45 ms normalised.
  manual->setValueNotifyingHost((45.0F + 100.0F) / 200.0F);

  // Drive the plug-in at real-time pace while a receiver reads the ring.
  das::sync::StreamReceiver receiver(ring);
  das::sync::StreamReceiver::Block block;
  const auto blockNs = static_cast<std::int64_t>(blockSize / rate * 1.0e9);
  auto next = nowNs();
  double phase {};
  int emitted {};
  double peak {};
  bool channelsMatch = true;
  std::int64_t worstTimestampError {};
  const auto stopAt = next + 3'000'000'000;
  while (next < stopAt) {
    for (int sample = 0; sample < blockSize; ++sample) {
      const auto value = static_cast<float>(0.25 * std::sin(phase));
      phase += 2.0 * std::numbers::pi * 440.0 / rate;
      audio.setSample(0, sample, value);
      audio.setSample(1, sample, value);
    }
    plugin->processBlock(audio, midi);
    next += blockNs;
    while (nowNs() < next) {
      while (receiver.poll(nowNs(), block)) {
        ++emitted;
        for (std::size_t i = 0; i < block.frames; ++i) {
          peak = std::max(peak, static_cast<double>(std::abs(block.samples[i * 2])));
          channelsMatch = channelsMatch && block.samples[i * 2] == block.samples[i * 2 + 1];
        }
        // Timestamp = due time of the packet's first block - delay - 45 ms. The
        // newest block is due at most a few ms ago; a batch packet starts
        // (frames - one block) earlier.
        const auto batchLeadNs = static_cast<std::int64_t>(block.frames - das::sync::StreamReceiver::kBlockFrames) *
                                 1'000'000'000LL / 48000;
        const auto expected = nowNs() - receiver.delayNs() - 45'000'000 - batchLeadNs;
        worstTimestampError = std::max<std::int64_t>(
            worstTimestampError, std::llabs(static_cast<std::int64_t>(block.timestampNs) - expected));
      }
      std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
  }

  das::transport::ClockSnapshot clock;
  if (!ring.readClock(clock)) return fail(16, "no clock model published");
  const auto control = ring.producerControl();
  const auto status = ring.consumerStatus();
  std::cout << "clock period " << clock.periodNs << " ns, epoch " << clock.epoch
            << ", lead " << control.leadNs / 1.0e6 << " ms, receiver delay "
            << receiver.delayNs() / 1.0e6 << " ms, emitted " << emitted
            << ", peak " << peak << ", worst ts error " << worstTimestampError / 1.0e6 << " ms\n";
  if (std::abs(clock.periodNs - 1.0e9 / 48000.0) > 1.0e9 / 48000.0 * 0.002)
    return fail(17, "clock period far from 48 kHz");
  // The value travels as a normalised float, so allow 0.1 ms of rounding.
  if (!control.autoCompensation || std::llabs(control.manualOffsetNs - 45'000'000) > 100'000 ||
      control.leadNs <= 0)
    return fail(18, "producer controls not published: auto " + std::to_string(control.autoCompensation) +
                        " manual " + std::to_string(control.manualOffsetNs) + " lead " +
                        std::to_string(control.leadNs));
  if (status.delayNs != receiver.delayNs()) return fail(19, "consumer status not published");
  if (emitted < 250) return fail(9, "receiver emitted too few blocks: " + std::to_string(emitted));
  if (peak < 0.2 || peak > 0.3) return fail(12, "received audio level is wrong");
  if (!channelsMatch) return fail(11, "left and right differ for a mono signal");
  if (worstTimestampError > 15'000'000) return fail(20, "timestamps do not follow DAW time");

  // Auto compensation off is published too.
  automatic->setValueNotifyingHost(0.0F);
  plugin->processBlock(audio, midi);
  if (ring.producerControl().autoCompensation) return fail(21, "auto compensation flag not published");

  plugin->releaseResources();
  return 0;
}
