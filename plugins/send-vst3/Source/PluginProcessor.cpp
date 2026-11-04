// SPDX-License-Identifier: AGPL-3.0-only
// Modified for DAS Kai (2026-11-04): timestamped transport with a clock
// model, latency compensation controls, macOS only. See MODIFICATIONS.md.
#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <time.h>

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
constexpr auto kTestNamespaceVariable = "KINU_STREAM_TEST_NAMESPACE";

std::wstring namespacedName(const std::wstring_view base) {
  std::wstring name(base);
  if (const auto* suffix = std::getenv(kTestNamespaceVariable);
      suffix != nullptr && *suffix != '\0') {
    name.push_back(L'.');
    while (*suffix != '\0') name.push_back(static_cast<unsigned char>(*suffix++));
  }
  return name;
}

std::string senderLockPath() {
  // The per-user temporary folder is private to this user, unlike /tmp, so no
  // other account can plant a link at the lock path.
  char directory[1024] {};
  std::string path;
  if (confstr(_CS_DARWIN_USER_TEMP_DIR, directory, sizeof(directory)) > 0)
    path = directory;
  else
    path = "/tmp/";
  if (!path.empty() && path.back() != '/') path.push_back('/');
  path += "com.keinoaza.kinustream.send.lock";
  if (const auto* suffix = std::getenv(kTestNamespaceVariable);
      suffix != nullptr && *suffix != '\0') {
    path.push_back('.');
    while (*suffix != '\0') {
      const auto value = static_cast<unsigned char>(*suffix++);
      const bool safe = (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
                        (value >= '0' && value <= '9') || value == '-' || value == '_';
      path.push_back(safe ? static_cast<char>(value) : '_');
    }
  }
  return path;
}

std::int64_t monotonicNowNs() noexcept {
  // Same clock as OBS's os_gettime_ns() on macOS.
  return static_cast<std::int64_t>(clock_gettime_nsec_np(CLOCK_UPTIME_RAW));
}

std::unique_ptr<das::transport::RingBuffer> prepareSharedRing(
    das::transport::NamedSharedMemory& memory, const std::wstring_view name,
    const std::size_t bytes) {
  memory = das::transport::NamedSharedMemory::create(namespacedName(name), bytes);
  if (!memory.isOpen()) return {};
  auto ring = std::make_unique<das::transport::RingBuffer>(memory.storage());
  if (!ring->isCompatible()) {
    if (!das::transport::RingBuffer::initialize(memory.storage(),
                                                das::transport::kAudioCapacityFrames,
                                                das::transport::kAudioChannels,
                                                das::transport::kAudioSampleRate))
      return {};
    ring = std::make_unique<das::transport::RingBuffer>(memory.storage());
  }
  if (!ring->isCompatible()) return {};
  return ring;
}
} // namespace

DasSendProcessor::DasSendProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters_(*this, nullptr, "KinuStream", createLayout()) {
  autoCompensation_ = parameters_.getRawParameterValue(kAutoCompensationId);
  manualOffsetMs_ = parameters_.getRawParameterValue(kManualOffsetId);

  const auto lockPath = senderLockPath();
  const auto guard = ::open(lockPath.c_str(), O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC,
                            S_IRUSR | S_IWUSR);
  if (guard < 0 || flock(guard, LOCK_EX | LOCK_NB) != 0) {
    primarySender_ = false;
    if (guard >= 0) ::close(guard);
  } else {
    senderGuard_ = guard;
  }
}

DasSendProcessor::~DasSendProcessor() {
  releaseResources();
  if (senderGuard_ != -1) {
    static_cast<void>(flock(static_cast<int>(senderGuard_), LOCK_UN));
    ::close(static_cast<int>(senderGuard_));
  }
}

juce::AudioProcessorValueTreeState::ParameterLayout DasSendProcessor::createLayout() {
  juce::AudioProcessorValueTreeState::ParameterLayout layout;
  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID {kAutoCompensationId, 1}, "Auto Compensation", true));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID {kManualOffsetId, 1}, "Manual Offset",
      juce::NormalisableRange<float>(kManualOffsetMinMs, kManualOffsetMaxMs, 0.1F), 0.0F,
      juce::AudioParameterFloatAttributes().withLabel("ms")));
  return layout;
}

bool DasSendProcessor::usesJapanese() const {
  return parameters_.state.getProperty(kLanguageProperty, "ja").toString() != "en";
}

void DasSendProcessor::setUsesJapanese(const bool japanese) {
  parameters_.state.setProperty(kLanguageProperty, japanese ? "ja" : "en", nullptr);
}

void DasSendProcessor::getStateInformation(juce::MemoryBlock& destination) {
  if (const auto xml = parameters_.copyState().createXml())
    copyXmlToBinary(*xml, destination);
}

void DasSendProcessor::setStateInformation(const void* data, const int size) {
  if (const auto xml = getXmlFromBinary(data, size);
      xml != nullptr && xml->hasTagName(parameters_.state.getType()))
    parameters_.replaceState(juce::ValueTree::fromXml(*xml));
}

void DasSendProcessor::prepareToPlay(const double sampleRate, const int samplesPerBlock) {
  releaseResources();
  bypassed_.store(false, std::memory_order_relaxed);
  sampleRate_.store(sampleRate);
  if (primarySender_) {
    const auto bytes = das::transport::requiredBytes(das::transport::kAudioCapacityFrames,
                                                      das::transport::kAudioChannels);
    obsRing_ = prepareSharedRing(obsMemory_, das::transport::kObsAudioMappingName, bytes);
  }
  // DAWs may still deliver a larger block occasionally; the sender chunks it.
  const auto prepared = sender_.prepare(
      sampleRate, static_cast<std::size_t>(std::max(1, samplesPerBlock)), obsRing_.get());
  sampleRateSupported_.store(prepared);
  transportReady_.store(primarySender_ && prepared && obsRing_ != nullptr);
}

void DasSendProcessor::releaseResources() {
  sender_.detach();
  obsRing_.reset();
  obsMemory_ = {};
  transportReady_.store(false);
}

bool DasSendProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
  const auto input = layouts.getMainInputChannelSet();
  return input == layouts.getMainOutputChannelSet() &&
         (input == juce::AudioChannelSet::mono() || input == juce::AudioChannelSet::stereo());
}

void DasSendProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
  juce::ScopedNoDenormals noDenormals;
  bypassed_.store(false, std::memory_order_relaxed);
  if (isNonRealtime()) {
    // Exports must not reach the live stream; the clock re-anchors afterwards.
    offline_.store(true, std::memory_order_relaxed);
    sender_.markDiscontinuity();
    return;
  }
  offline_.store(false, std::memory_order_relaxed);
  sendBlock(&buffer, buffer.getNumSamples());
}

void DasSendProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer,
                                            juce::MidiBuffer& midi) {
  bypassed_.store(true, std::memory_order_relaxed);
  juce::AudioProcessor::processBlockBypassed(buffer, midi);
  if (isNonRealtime()) {
    sender_.markDiscontinuity();
    return;
  }
  // Keep the stream clock running with silence so un-bypassing needs no resync.
  sendBlock(nullptr, buffer.getNumSamples());
}

void DasSendProcessor::sendBlock(const juce::AudioBuffer<float>* const audio,
                                 const int numSamples) noexcept {
  if (!primarySender_ || !obsRing_ || numSamples <= 0 ||
      !sampleRateSupported_.load(std::memory_order_relaxed))
    return;
  const auto nowNs = monotonicNowNs();
  const auto manualMs = std::clamp(manualOffsetMs_->load(std::memory_order_relaxed),
                                   kManualOffsetMinMs, kManualOffsetMaxMs);
  sender_.setControl(autoCompensation_->load(std::memory_order_relaxed) >= 0.5F,
                     static_cast<std::int64_t>(std::llround(static_cast<double>(manualMs) * 1.0e6)));

  const float* left {};
  const float* right {};
  if (audio != nullptr && audio->getNumChannels() > 0) {
    left = audio->getReadPointer(0);
    right = audio->getReadPointer(std::min(1, audio->getNumChannels() - 1));
  }
  sender_.process(left, right, static_cast<std::size_t>(numSamples), nowNs);

  driftPpm_.store(sender_.driftPpm(), std::memory_order_relaxed);
  clockLocked_.store(sender_.clockLocked(), std::memory_order_relaxed);
  const auto status = obsRing_->consumerStatus();
  obsHeartbeat_.store(obsRing_->consumerHeartbeat(), std::memory_order_relaxed);
  obsDelayNs_.store(status.delayNs, std::memory_order_relaxed);
  obsAppliedOffsetNs_.store(status.appliedOffsetNs, std::memory_order_relaxed);
  obsResyncs_.store(status.resyncs, std::memory_order_relaxed);
  obsUnderruns_.store(status.underruns, std::memory_order_relaxed);
}

das::transport::ConsumerStatus DasSendProcessor::obsStatus() const noexcept {
  return {obsDelayNs_.load(std::memory_order_relaxed),
          obsAppliedOffsetNs_.load(std::memory_order_relaxed),
          obsResyncs_.load(std::memory_order_relaxed),
          obsUnderruns_.load(std::memory_order_relaxed)};
}

juce::AudioProcessorEditor* DasSendProcessor::createEditor() {
  return new DasSendEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DasSendProcessor(); }
