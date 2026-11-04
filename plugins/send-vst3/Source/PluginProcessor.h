// SPDX-License-Identifier: AGPL-3.0-only
// Modified for DAS Kai (2026-11-04): timestamped transport with a clock
// model, latency compensation controls, macOS only. See MODIFICATIONS.md.
#pragma once

#include <das/audio/StreamSender.h>
#include <das/transport/NamedSharedMemory.h>
#include <das/transport/RingBuffer.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

class DasSendProcessor final : public juce::AudioProcessor {
public:
  static constexpr const char* kAutoCompensationId = "autoCompensation";
  static constexpr const char* kManualOffsetId = "manualOffsetMs";
  static constexpr float kManualOffsetMinMs = -500.0F;
  // +500 ms covers Bluetooth headphones; with the automatic part the total
  // stays under the receiver's 850 ms ceiling (OBS buffers at most 960 ms).
  static constexpr float kManualOffsetMaxMs = 500.0F;

  DasSendProcessor();
  ~DasSendProcessor() override;

  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override;
  bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
  void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
  void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
  juce::AudioProcessorEditor* createEditor() override;
  bool hasEditor() const override { return true; }
  const juce::String getName() const override { return JucePlugin_Name; }
  bool acceptsMidi() const override { return false; }
  bool producesMidi() const override { return false; }
  bool isMidiEffect() const override { return false; }
  double getTailLengthSeconds() const override { return 0.0; }
  int getNumPrograms() override { return 1; }
  int getCurrentProgram() override { return 0; }
  void setCurrentProgram(int) override {}
  const juce::String getProgramName(int) override { return {}; }
  void changeProgramName(int, const juce::String&) override {}
  void getStateInformation(juce::MemoryBlock&) override;
  void setStateInformation(const void*, int) override;

  [[nodiscard]] juce::AudioProcessorValueTreeState& parameters() noexcept { return parameters_; }
  // Window language; saved with the DAW project. Japanese unless set to English.
  // Message thread only.
  static constexpr const char* kLanguageProperty = "uiLanguage";
  [[nodiscard]] bool usesJapanese() const;
  void setUsesJapanese(bool japanese);
  [[nodiscard]] double currentSampleRate() const noexcept { return sampleRate_.load(); }
  [[nodiscard]] bool sampleRateSupported() const noexcept { return sampleRateSupported_.load(); }
  [[nodiscard]] bool transportReady() const noexcept { return transportReady_.load(); }
  [[nodiscard]] bool isBypassed() const noexcept { return bypassed_.load(); }
  [[nodiscard]] bool isPrimarySender() const noexcept { return primarySender_; }
  [[nodiscard]] bool isRenderingOffline() const noexcept { return offline_.load(); }
  [[nodiscard]] std::uint64_t obsConsumerHeartbeat() const noexcept {
    return obsHeartbeat_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] das::transport::ConsumerStatus obsStatus() const noexcept;
  [[nodiscard]] double clockDriftPpm() const noexcept { return driftPpm_.load(); }
  [[nodiscard]] bool clockLocked() const noexcept { return clockLocked_.load(); }

private:
  static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
  void sendBlock(const juce::AudioBuffer<float>* audio, int numSamples) noexcept;

  juce::AudioProcessorValueTreeState parameters_;
  std::atomic<float>* autoCompensation_ {};
  std::atomic<float>* manualOffsetMs_ {};

  das::transport::NamedSharedMemory obsMemory_;
  std::unique_ptr<das::transport::RingBuffer> obsRing_;
  das::audio::StreamSender sender_;

  std::atomic<double> sampleRate_ {48000.0};
  std::atomic<bool> sampleRateSupported_ {true};
  std::atomic<bool> transportReady_ {};
  std::atomic<bool> bypassed_ {};
  std::atomic<bool> offline_ {};
  std::atomic<double> driftPpm_ {};
  std::atomic<bool> clockLocked_ {};
  // Copied from shared memory on the audio thread so the editor never touches
  // the mapping, which releaseResources() may unmap on another thread.
  std::atomic<std::uint64_t> obsHeartbeat_ {};
  std::atomic<std::int64_t> obsDelayNs_ {};
  std::atomic<std::int64_t> obsAppliedOffsetNs_ {};
  std::atomic<std::uint64_t> obsResyncs_ {};
  std::atomic<std::uint64_t> obsUnderruns_ {};
  std::intptr_t senderGuard_ {-1};
  bool primarySender_ {true};
};
