// SPDX-License-Identifier: AGPL-3.0-only
// Modified for Kinu Stream (2026-11-04): Discord card replaced by the sync
// panel (latency display, auto compensation, manual offset). See MODIFICATIONS.md.
#include "PluginEditor.h"

#include <das/sync/StreamReceiver.h>

#include <cmath>

namespace {
struct Theme {
  juce::Colour parchment;
  juce::Colour card;
  juce::Colour ink;
  juce::Colour graphite;
  juce::Colour linen;
  juce::Colour smoke;
  juce::Colour markerYellow;
  juce::Colour detail;
};

Theme themeFor(const bool darkMode) {
  if (darkMode) {
    return {juce::Colour(0xff151411), juce::Colour(0xff211f1b),
            juce::Colour(0xfff7f3eb), juce::Colour(0xffbcb4a5),
            juce::Colour(0xff484239), juce::Colour(0xff3a3630),
            juce::Colour(0xfff2cd32), juce::Colour(0xff2a2721)};
  }
  return {juce::Colour(0xffded5c8), juce::Colour(0xfff3eee7),
          juce::Colour(0xff000000), juce::Colour(0xff6a6559),
          juce::Colour(0xffc8beb1), juce::Colour(0xffddd5ca),
          juce::Colour(0xffffdd33), juce::Colour(0xffd2c7b8)};
}

void drawRaisedPanel(juce::Graphics& graphics, juce::Rectangle<float> bounds,
                     const float radius, const Theme& theme) {
  graphics.setColour(theme.card);
  graphics.fillRoundedRectangle(bounds, radius);
  graphics.setColour(theme.linen);
  graphics.drawRoundedRectangle(bounds.reduced(0.5F), radius, 1.0F);
}

void drawWaveIcon(juce::Graphics& graphics, juce::Rectangle<float> bounds,
                  const juce::Colour colour) {
  const auto centre = bounds.getCentre();
  graphics.setColour(colour.withAlpha(0.11F));
  graphics.fillEllipse(bounds);
  graphics.setColour(colour);
  graphics.drawEllipse(bounds.reduced(0.5F), 1.4F);
  const float heights[] {8.0F, 16.0F, 23.0F, 14.0F, 7.0F};
  auto x = centre.x - 9.0F;
  for (const auto height : heights) {
    graphics.drawLine(x, centre.y - height * 0.5F, x, centre.y + height * 0.5F, 2.1F);
    x += 4.5F;
  }
}

juce::String formatMs(const double milliseconds, const bool signedValue) {
  const auto text = juce::String(std::abs(milliseconds), 1);
  if (!signedValue) return text;
  return (milliseconds < -0.05 ? "-" : "+") + text;
}
} // namespace

DasSendEditor::DasSendEditor(DasSendProcessor& processor)
    : AudioProcessorEditor(processor), processor_(processor) {
  darkMode_ = juce::Desktop::getInstance().isDarkModeActive();

  title_.setText("Kinu Stream", juce::dontSendNotification);
  title_.setFont(juce::FontOptions(25.0F, juce::Font::bold));
  addAndMakeVisible(title_);

  subtitle_.setText("DAW  >  OBS", juce::dontSendNotification);
  subtitle_.setFont(juce::FontOptions(11.0F, juce::Font::bold));
  subtitle_.setJustificationType(juce::Justification::centredRight);
  addAndMakeVisible(subtitle_);

  for (auto* heading : {&obsTitle_, &syncTitle_, &manualTitle_}) {
    heading->setFont(juce::FontOptions(13.0F, juce::Font::bold));
    addAndMakeVisible(*heading);
  }
  obsTitle_.setText("OBS", juce::dontSendNotification);
  syncTitle_.setText("SYNC", juce::dontSendNotification);
  manualTitle_.setText("MANUAL", juce::dontSendNotification);

  obsStatus_.setFont(juce::FontOptions(18.0F, juce::Font::bold));
  obsStatus_.setJustificationType(juce::Justification::centred);
  addAndMakeVisible(obsStatus_);

  syncTotal_.setFont(juce::FontOptions(30.0F, juce::Font::bold));
  addAndMakeVisible(syncTotal_);
  for (auto* small : {&clockInfo_, &syncDirection_, &syncBreakdown_, &detail_}) {
    small->setFont(juce::FontOptions(12.0F));
    addAndMakeVisible(*small);
  }
  syncDirection_.setFont(juce::FontOptions(11.0F, juce::Font::bold));
  detail_.setJustificationType(juce::Justification::centredRight);

  addAndMakeVisible(autoButton_);
  autoAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
      processor_.parameters(), DasSendProcessor::kAutoCompensationId, autoButton_);

  manualSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 92, 26);
  manualSlider_.setVelocityModeParameters(0.25, 1, 0.0, false, juce::ModifierKeys::shiftModifier);
  addAndMakeVisible(manualSlider_);
  manualAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
      processor_.parameters(), DasSendProcessor::kManualOffsetId, manualSlider_);
  // Custom text only after the attachment, which installs its own formatter.
  manualSlider_.textFromValueFunction = [](const double value) {
    return formatMs(value, true) + " ms";
  };
  manualSlider_.valueFromTextFunction = [](const juce::String& text) {
    return text.retainCharacters("+-.0123456789").getDoubleValue();
  };
  manualSlider_.updateText();
  if (auto* parameter = processor_.parameters().getParameter(DasSendProcessor::kManualOffsetId))
    manualSlider_.setDoubleClickReturnValue(
        true, parameter->convertFrom0to1(parameter->getDefaultValue()));

  setSize(620, 330);
  applyTheme();
  juce::Desktop::getInstance().addDarkModeSettingListener(this);
  startTimerHz(5);
  timerCallback();
}

DasSendEditor::~DasSendEditor() {
  juce::Desktop::getInstance().removeDarkModeSettingListener(this);
  stopTimer();
}

juce::Colour DasSendEditor::colourFor(const VisualState state) const {
  const auto theme = themeFor(darkMode_);
  switch (state) {
    case VisualState::active:
      return darkMode_ ? juce::Colour(0xff95c96a) : juce::Colour(0xff3e6b15);
    case VisualState::waiting: return theme.graphite;
    case VisualState::warning:
      return darkMode_ ? theme.markerYellow : juce::Colour(0xffaa7e2e);
    case VisualState::error:
      return darkMode_ ? juce::Colour(0xffff7b59) : juce::Colour(0xffc74725);
  }
  return theme.graphite;
}

void DasSendEditor::darkModeSettingChanged() {
  darkMode_ = juce::Desktop::getInstance().isDarkModeActive();
  applyTheme();
}

void DasSendEditor::applyTheme() {
  const auto theme = themeFor(darkMode_);
  title_.setColour(juce::Label::textColourId, theme.ink);
  syncTotal_.setColour(juce::Label::textColourId, theme.ink);
  for (auto* label : {&subtitle_, &obsTitle_, &syncTitle_, &manualTitle_, &clockInfo_,
                      &syncDirection_, &syncBreakdown_, &detail_})
    label->setColour(juce::Label::textColourId, theme.graphite);

  autoButton_.setColour(juce::ToggleButton::textColourId, theme.ink);
  autoButton_.setColour(juce::ToggleButton::tickColourId, theme.ink);
  autoButton_.setColour(juce::ToggleButton::tickDisabledColourId, theme.graphite);
  manualSlider_.setColour(juce::Slider::backgroundColourId, theme.smoke);
  manualSlider_.setColour(juce::Slider::trackColourId, theme.markerYellow);
  manualSlider_.setColour(juce::Slider::thumbColourId, theme.ink);
  manualSlider_.setColour(juce::Slider::textBoxTextColourId, theme.ink);
  manualSlider_.setColour(juce::Slider::textBoxBackgroundColourId, theme.card);
  manualSlider_.setColour(juce::Slider::textBoxOutlineColourId, theme.linen);
  timerCallback();
}

void DasSendEditor::updateStatus(const juce::String& value, const VisualState state) {
  const auto theme = themeFor(darkMode_);
  obsState_ = state;
  obsStatus_.setText(value, juce::dontSendNotification);
  obsStatus_.setColour(juce::Label::textColourId,
                       state == VisualState::active || state == VisualState::error
                           ? juce::Colours::white
                           : state == VisualState::warning ? juce::Colours::black : theme.ink);
}

void DasSendEditor::paint(juce::Graphics& graphics) {
  const auto theme = themeFor(darkMode_);
  graphics.fillAll(theme.parchment);

  auto marker = juce::Path();
  marker.startNewSubPath(24.0F, 46.0F);
  marker.lineTo(181.0F, 42.0F);
  marker.lineTo(185.0F, 58.0F);
  marker.lineTo(22.0F, 61.0F);
  marker.closeSubPath();
  graphics.setColour(theme.markerYellow.withAlpha(0.9F));
  graphics.fillPath(marker);

  drawRaisedPanel(graphics, {24.0F, 82.0F, 278.0F, 124.0F}, 12.0F, theme);
  drawRaisedPanel(graphics, {318.0F, 82.0F, 278.0F, 124.0F}, 12.0F, theme);
  drawRaisedPanel(graphics, {24.0F, 220.0F, 572.0F, 92.0F}, 12.0F, theme);

  const auto statusFill = [&theme](const VisualState state) {
    switch (state) {
      case VisualState::active: return juce::Colour(0xff3e6b15);
      case VisualState::waiting: return theme.smoke;
      case VisualState::warning: return theme.markerYellow;
      case VisualState::error: return juce::Colour(0xffc74725);
    }
    return theme.smoke;
  };
  graphics.setColour(statusFill(obsState_));
  graphics.fillRoundedRectangle(106.0F, 120.0F, 168.0F, 32.0F, 16.0F);
  drawWaveIcon(graphics, juce::Rectangle<float>(44.0F, 104.0F, 48.0F, 48.0F),
               colourFor(obsState_));
}

void DasSendEditor::resized() {
  title_.setBounds(26, 19, 250, 40);
  subtitle_.setBounds(360, 25, 234, 26);

  obsTitle_.setBounds(108, 94, 168, 22);
  obsStatus_.setBounds(119, 117, 144, 38);
  clockInfo_.setBounds(42, 168, 250, 24);

  syncTitle_.setBounds(338, 92, 120, 20);
  syncTotal_.setBounds(336, 110, 250, 40);
  syncDirection_.setBounds(338, 148, 250, 18);
  syncBreakdown_.setBounds(338, 170, 250, 24);

  autoButton_.setBounds(40, 232, 150, 30);
  detail_.setBounds(200, 232, 380, 30);
  manualTitle_.setBounds(42, 272, 80, 28);
  manualSlider_.setBounds(118, 272, 466, 28);
}

void DasSendEditor::timerCallback() {
  const auto now = juce::Time::getMillisecondCounterHiRes();
  const auto heartbeat = processor_.obsConsumerHeartbeat();
  if (heartbeat != lastObsHeartbeat_) {
    lastObsHeartbeat_ = heartbeat;
    lastObsActivityMs_ = now;
  }
  const auto obsActive = lastObsActivityMs_ > 0.0 && now - lastObsActivityMs_ < 1500.0;
  const auto status = processor_.obsStatus();
  obsConnected_ = obsActive && status.delayNs > 0;

  if (!processor_.isPrimarySender()) {
    updateStatus("1 ONLY", VisualState::warning);
    detail_.setText("Kinu Stream  x  1 per user", juce::dontSendNotification);
  } else if (processor_.isRenderingOffline()) {
    updateStatus("EXPORT", VisualState::waiting);
    detail_.setText("Not sent while exporting", juce::dontSendNotification);
  } else if (!processor_.sampleRateSupported()) {
    updateStatus("8-384 kHz", VisualState::error);
    detail_.setText({}, juce::dontSendNotification);
  } else if (!processor_.transportReady()) {
    updateStatus("!", VisualState::error);
    detail_.setText("RESTART   OBS  >  DAW", juce::dontSendNotification);
  } else if (processor_.isBypassed()) {
    updateStatus("BYPASS", VisualState::waiting);
    detail_.setText("Sending silence", juce::dontSendNotification);
  } else {
    updateStatus(obsActive ? "OK" : "WAIT", obsActive ? VisualState::active : VisualState::waiting);
    detail_.setText(obsActive ? "+ = earlier in OBS   double-click = 0"
                              : "Add source  Kinu Stream (DAW)  in OBS",
                    juce::dontSendNotification);
  }

  const auto ppm = juce::roundToInt(processor_.clockDriftPpm());
  clockInfo_.setText(processor_.clockLocked()
                         ? "CLOCK  " + juce::String(ppm >= 0 ? "+" : "") + juce::String(ppm) + " ppm"
                         : juce::String("CLOCK  locking..."),
                     juce::dontSendNotification);

  const auto autoOn = processor_.parameters()
                          .getRawParameterValue(DasSendProcessor::kAutoCompensationId)
                          ->load() >= 0.5F;
  const auto manualMs = static_cast<double>(
      processor_.parameters().getRawParameterValue(DasSendProcessor::kManualOffsetId)->load());
  if (obsConnected_) {
    const auto autoMs = autoOn ? static_cast<double>(status.delayNs) / 1.0e6 : 0.0;
    // Show what OBS actually uses; a new setting is applied after it settles.
    const auto appliedMs = static_cast<double>(status.appliedOffsetNs) / 1.0e6;
    const auto ceilingMs = static_cast<double>(das::sync::StreamReceiver::kMaximumAppliedOffsetNs) / 1.0e6;
    const auto pending = std::abs(appliedMs - std::min(autoMs + manualMs, ceilingMs)) > 0.05;
    syncTotal_.setText(formatMs(appliedMs, false) + " ms", juce::dontSendNotification);
    syncDirection_.setText(pending                    ? juce::String("APPLYING...")
                           : std::abs(appliedMs) < 0.05 ? juce::String("NO SHIFT")
                           : appliedMs > 0.0          ? juce::String("EARLIER IN OBS")
                                                      : juce::String("LATER IN OBS"),
                           juce::dontSendNotification);
    syncBreakdown_.setText((autoOn ? "AUTO " + formatMs(autoMs, false) : juce::String("AUTO off")) +
                               "   MANUAL " + formatMs(manualMs, true),
                           juce::dontSendNotification);
  } else {
    syncTotal_.setText("-- ms", juce::dontSendNotification);
    syncDirection_.setText("OBS NOT CONNECTED", juce::dontSendNotification);
    syncBreakdown_.setText(juce::String("MANUAL ") + formatMs(manualMs, true),
                           juce::dontSendNotification);
  }

  repaint();
}
