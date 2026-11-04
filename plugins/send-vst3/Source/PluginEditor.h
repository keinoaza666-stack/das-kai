// SPDX-License-Identifier: AGPL-3.0-only
// Modified for Kinu Stream (2026-11-04): Discord card replaced by the sync
// panel (latency display, auto compensation, manual offset). See MODIFICATIONS.md.
#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

class DasSendEditor final : public juce::AudioProcessorEditor,
                            private juce::Timer,
                            private juce::DarkModeSettingListener {
public:
  explicit DasSendEditor(DasSendProcessor& processor);
  ~DasSendEditor() override;
  void paint(juce::Graphics& graphics) override;
  void resized() override;

private:
  enum class VisualState { active, waiting, warning, error };

  void timerCallback() override;
  void darkModeSettingChanged() override;
  void applyTheme();
  void updateStatus(const juce::String& text, VisualState state);
  [[nodiscard]] juce::Colour colourFor(VisualState state) const;

  DasSendProcessor& processor_;
  bool darkMode_ {};
  VisualState obsState_ {VisualState::waiting};
  bool obsConnected_ {};
  juce::Label title_;
  juce::Label subtitle_;
  juce::Label obsTitle_;
  juce::Label obsStatus_;
  juce::Label clockInfo_;
  juce::Label syncTitle_;
  juce::Label syncTotal_;
  juce::Label syncDirection_;
  juce::Label syncBreakdown_;
  juce::Label manualTitle_;
  juce::Label detail_;
  juce::ToggleButton autoButton_ {"AUTO"};
  juce::Slider manualSlider_ {juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight};
  std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoAttachment_;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> manualAttachment_;
  std::uint64_t lastObsHeartbeat_ {};
  double lastObsActivityMs_ {};
};
