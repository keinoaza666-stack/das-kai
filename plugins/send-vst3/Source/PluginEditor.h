// SPDX-License-Identifier: AGPL-3.0-only
// Modified for DAS Kai (2026-11-04): Discord card replaced by the sync panel
// (latency display, auto compensation, manual offset); own colours and fonts
// ("sumi and jade"); Japanese/English labels. See MODIFICATIONS.md.
#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

// Fonts are the system's own, so nothing is embedded in the plug-in.
// macOS: Hiragino Maru Gothic ProN (a single weight, W4) and Menlo.
// Windows (2026-11-05): Yu Gothic UI and Consolas; JUCE falls back to
// another installed font for any character they lack.
namespace daskai::ui {
#if defined(_WIN32)
inline constexpr const char* kTextFont = "Yu Gothic UI";
inline constexpr const char* kNumberFont = "Consolas";
#else
inline constexpr const char* kTextFont = "Hiragino Maru Gothic ProN";
inline constexpr const char* kNumberFont = "Menlo";
#endif

class KaiLookAndFeel final : public juce::LookAndFeel_V4 {
public:
  void drawToggleButton(juce::Graphics& graphics, juce::ToggleButton& button,
                        bool highlighted, bool down) override;
  juce::Label* createSliderTextBox(juce::Slider& slider) override;
};

// Small text switch "日本語 ・ EN"; the selected side is drawn in the accent
// colour and underlined.
class LanguageSwitch final : public juce::Component {
public:
  LanguageSwitch();
  std::function<void(bool japanese)> onChange;
  void setJapanese(bool japanese);
  void setColours(juce::Colour selected, juce::Colour unselected);
  void paint(juce::Graphics& graphics) override;
  void mouseUp(const juce::MouseEvent& event) override;

private:
  bool japanese_ {true};
  juce::Colour selected_;
  juce::Colour unselected_;
};
} // namespace daskai::ui

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
  void applyLanguage();
  // Japanese text as UTF-8, English as ASCII.
  [[nodiscard]] juce::String text(const char* japanese, const char* english) const;
  void updateStatus(const juce::String& text, VisualState state);
  [[nodiscard]] juce::Colour colourFor(VisualState state) const;
  // Green within the normal range, yellow beyond it, red past what the OBS
  // side can follow smoothly.
  [[nodiscard]] juce::Colour clockColour(double ppm) const;
  void layoutClock();

  // Declared first so it outlives every child that uses it.
  daskai::ui::KaiLookAndFeel lookAndFeel_;
  DasSendProcessor& processor_;
  bool darkMode_ {};
  bool japanese_ {true};
  VisualState obsState_ {VisualState::waiting};
  bool obsConnected_ {};
  juce::Label title_;
  juce::Label subtitle_;
  juce::Label obsTitle_;
  juce::Label obsStatus_;
  juce::Label clockInfo_;  // "クロックのずれ" / "クロックを合わせています…"
  juce::Label clockValue_; // the reading, coloured
  juce::Label syncTitle_;
  juce::Label syncTotal_;
  juce::Label syncDirection_;
  juce::Label syncBreakdown_;
  juce::Label manualTitle_;
  juce::Label detail_;
  daskai::ui::LanguageSwitch languageSwitch_;
  juce::ToggleButton autoButton_;
  juce::Slider manualSlider_ {juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight};
  std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoAttachment_;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> manualAttachment_;
  std::uint64_t lastObsHeartbeat_ {};
  double lastObsActivityMs_ {};
};
