// SPDX-License-Identifier: AGPL-3.0-only
// Modified for DAS Kai (2026-11-04): Discord card replaced by the sync panel
// (latency display, auto compensation, manual offset); own colours and fonts
// ("sumi and jade"); Japanese/English labels. See MODIFICATIONS.md.
#include "PluginEditor.h"

#include <das/sync/StreamReceiver.h>

#include <cmath>
#include <cstdlib>

namespace {
using daskai::ui::kNumberFont;
using daskai::ui::kTextFont;

// "Sumi and jade": green-black ground, jade accent. Kept away from upstream
// DawAudioStreamer's warm charcoal and yellow so the two are easy to tell apart.
struct Theme {
  juce::Colour background;
  juce::Colour card;
  juce::Colour ink;
  juce::Colour muted;
  juce::Colour line;
  juce::Colour track;
  juce::Colour accent;
  juce::Colour idlePill;
  juce::Colour activePill;
  juce::Colour activeIcon;
  juce::Colour warning;
  juce::Colour warningIcon;
  juce::Colour error;
  juce::Colour errorIcon;
  // Clock reading: normal, beyond normal, beyond what OBS follows smoothly.
  juce::Colour clockGood;
  juce::Colour clockCaution;
  juce::Colour clockBad;
};

// An audio interface normally runs within this of the Mac's clock (tens of
// ppm are common).
constexpr double kClockNormalPpm = 100.0;
// Rare for a real clock; a reading this far from a normal clock is an error
// the receiver's residual correction (300 ppm) cannot absorb, so OBS breaks.
constexpr double kClockAbnormalPpm = das::sync::StreamReceiver::kMaximumCorrection * 1.0e6;

Theme themeFor(const bool darkMode) {
  if (darkMode) {
    return {juce::Colour(0xff0f1211), juce::Colour(0xff181d1b), juce::Colour(0xffe9efec),
            juce::Colour(0xff8fa199), juce::Colour(0xff27302c), juce::Colour(0xff26302c),
            juce::Colour(0xff36c2a0), juce::Colour(0xff222a27), juce::Colour(0xff1f8f74),
            juce::Colour(0xff5fd8b9), juce::Colour(0xffe0a43a), juce::Colour(0xffe0a43a),
            juce::Colour(0xffd9534f), juce::Colour(0xffff8a80), juce::Colour(0xff58d68d),
            juce::Colour(0xffefcf4f), juce::Colour(0xffff6b63)};
  }
  return {juce::Colour(0xffeef3f1), juce::Colour(0xfffafcfb), juce::Colour(0xff142019),
          juce::Colour(0xff5d7068), juce::Colour(0xffc9d6d0), juce::Colour(0xffdbe5e0),
          juce::Colour(0xff1e9e80), juce::Colour(0xffdde7e2), juce::Colour(0xff1e8a70),
          juce::Colour(0xff1e8a70), juce::Colour(0xffe0a43a), juce::Colour(0xffa8721f),
          juce::Colour(0xffd9534f), juce::Colour(0xffc0392b), juce::Colour(0xff1e8a4c),
          juce::Colour(0xff9a7400), juce::Colour(0xffc0392b)};
}

bool resolveDarkMode() {
  // Development override for screenshots of both themes.
  if (const auto* forced = std::getenv("DAS_KAI_UI_THEME"); forced != nullptr) {
    if (juce::String(forced) == "light") return false;
    if (juce::String(forced) == "dark") return true;
  }
  return juce::Desktop::getInstance().isDarkModeActive();
}

juce::FontOptions textFont(const float size) { return juce::FontOptions(kTextFont, size, juce::Font::plain); }
juce::FontOptions numberFont(const float size, const bool bold) {
  return juce::FontOptions(kNumberFont, size, bold ? juce::Font::bold : juce::Font::plain);
}

void drawRaisedPanel(juce::Graphics& graphics, juce::Rectangle<float> bounds,
                     const float radius, const Theme& theme) {
  graphics.setColour(theme.card);
  graphics.fillRoundedRectangle(bounds, radius);
  graphics.setColour(theme.line);
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

namespace daskai::ui {

void KaiLookAndFeel::drawToggleButton(juce::Graphics& graphics, juce::ToggleButton& button,
                                      const bool highlighted, const bool down) {
  const auto fontSize = juce::jmin(15.0F, static_cast<float>(button.getHeight()) * 0.75F);
  const auto tickWidth = fontSize * 1.1F;
  drawTickBox(graphics, button, 4.0F, (static_cast<float>(button.getHeight()) - tickWidth) * 0.5F,
              tickWidth, tickWidth, button.getToggleState(), button.isEnabled(), highlighted, down);
  graphics.setColour(button.findColour(juce::ToggleButton::textColourId));
  graphics.setFont(textFont(fontSize + 1.0F));
  if (!button.isEnabled()) graphics.setOpacity(0.5F);
  graphics.drawFittedText(button.getButtonText(),
                          button.getLocalBounds()
                              .withTrimmedLeft(juce::roundToInt(tickWidth) + 10)
                              .withTrimmedRight(2),
                          juce::Justification::centredLeft, 1);
}

juce::Label* KaiLookAndFeel::createSliderTextBox(juce::Slider& slider) {
  auto* label = juce::LookAndFeel_V4::createSliderTextBox(slider);
  label->setFont(numberFont(13.0F, false));
  return label;
}

LanguageSwitch::LanguageSwitch() { setMouseCursor(juce::MouseCursor::PointingHandCursor); }

void LanguageSwitch::setJapanese(const bool japanese) {
  japanese_ = japanese;
  repaint();
}

void LanguageSwitch::setColours(const juce::Colour selected, const juce::Colour unselected) {
  selected_ = selected;
  unselected_ = unselected;
  repaint();
}

namespace {
// Fixed slots sized for 12 pt text: "日本語" needs about 37 px.
constexpr float kJapaneseSlot = 46.0F;
constexpr float kSeparatorSlot = 12.0F;
} // namespace

void LanguageSwitch::paint(juce::Graphics& graphics) {
  const auto bounds = getLocalBounds().toFloat();
  const auto japaneseArea = bounds.withWidth(kJapaneseSlot);
  const auto separatorArea = bounds.withX(kJapaneseSlot).withWidth(kSeparatorSlot);
  const auto englishArea = bounds.withTrimmedLeft(kJapaneseSlot + kSeparatorSlot);
  graphics.setFont(textFont(12.0F));
  const auto draw = [&](const juce::String& label, const juce::Rectangle<float> area, const bool selected) {
    graphics.setColour(selected ? selected_ : unselected_);
    graphics.drawText(label, area, juce::Justification::centred);
    if (selected) {
      const auto width = juce::GlyphArrangement::getStringWidth(graphics.getCurrentFont(), label);
      graphics.fillRect(area.getCentreX() - width * 0.5F, area.getBottom() - 2.0F, width, 1.5F);
    }
  };
  draw(juce::String(juce::CharPointer_UTF8("日本語")), japaneseArea, japanese_);
  graphics.setColour(unselected_);
  graphics.drawText(juce::String(juce::CharPointer_UTF8("・")), separatorArea,
                    juce::Justification::centred);
  draw("EN", englishArea, !japanese_);
}

void LanguageSwitch::mouseUp(const juce::MouseEvent& event) {
  if (!getLocalBounds().contains(event.getPosition())) return;
  const auto japanese = static_cast<float>(event.x) < kJapaneseSlot + kSeparatorSlot * 0.5F;
  if (japanese == japanese_) return;
  setJapanese(japanese);
  if (onChange) onChange(japanese);
}

} // namespace daskai::ui

DasSendEditor::DasSendEditor(DasSendProcessor& processor)
    : AudioProcessorEditor(processor), processor_(processor) {
  setLookAndFeel(&lookAndFeel_);
  darkMode_ = resolveDarkMode();

  title_.setText("DAS Kai", juce::dontSendNotification);
  title_.setFont(textFont(26.0F));
  addAndMakeVisible(title_);

  subtitle_.setText("DAW  >  OBS", juce::dontSendNotification);
  subtitle_.setFont(textFont(11.0F));
  subtitle_.setJustificationType(juce::Justification::centredRight);
  addAndMakeVisible(subtitle_);

  for (auto* heading : {&obsTitle_, &syncTitle_, &manualTitle_}) {
    heading->setFont(textFont(13.0F));
    addAndMakeVisible(*heading);
  }
  obsTitle_.setText("OBS", juce::dontSendNotification);

  japanese_ = processor_.usesJapanese();
  languageSwitch_.setJapanese(japanese_);
  languageSwitch_.onChange = [this](const bool japanese) {
    processor_.setUsesJapanese(japanese);
    japanese_ = japanese;
    applyLanguage();
  };
  addAndMakeVisible(languageSwitch_);

  obsStatus_.setFont(textFont(18.0F));
  obsStatus_.setJustificationType(juce::Justification::centred);
  addAndMakeVisible(obsStatus_);

  syncTotal_.setFont(numberFont(30.0F, true));
  addAndMakeVisible(syncTotal_);
  for (auto* small : {&clockInfo_, &clockValue_, &syncDirection_, &syncBreakdown_, &detail_}) {
    small->setFont(textFont(12.0F));
    addAndMakeVisible(*small);
  }
  detail_.setJustificationType(juce::Justification::centredRight);

  addAndMakeVisible(autoButton_);
  autoAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
      processor_.parameters(), DasSendProcessor::kAutoCompensationId, autoButton_);

  manualSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 92, 26);
  // Hold Shift for fine adjustment (velocity mode at a quarter of the speed).
  manualSlider_.setVelocityModeParameters(0.25, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
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
  applyLanguage();
  applyTheme();
  juce::Desktop::getInstance().addDarkModeSettingListener(this);
  startTimerHz(5);
  timerCallback();
}

DasSendEditor::~DasSendEditor() {
  juce::Desktop::getInstance().removeDarkModeSettingListener(this);
  stopTimer();
  setLookAndFeel(nullptr);
}

juce::Colour DasSendEditor::colourFor(const VisualState state) const {
  const auto theme = themeFor(darkMode_);
  switch (state) {
    case VisualState::active: return theme.activeIcon;
    case VisualState::waiting: return theme.muted;
    case VisualState::warning: return theme.warningIcon;
    case VisualState::error: return theme.errorIcon;
  }
  return theme.muted;
}

juce::Colour DasSendEditor::clockColour(const double ppm) const {
  const auto theme = themeFor(darkMode_);
  const auto size = std::abs(ppm);
  if (size <= kClockNormalPpm) return theme.clockGood;
  if (size <= kClockAbnormalPpm) return theme.clockCaution;
  return theme.clockBad;
}

void DasSendEditor::layoutClock() {
  // The reading follows the caption, whose width depends on the language.
  const auto border = clockInfo_.getBorderSize();
  const auto width = static_cast<int>(std::ceil(
      juce::GlyphArrangement::getStringWidth(clockInfo_.getFont(), clockInfo_.getText())));
  clockInfo_.setBounds(42, 168, width + border.getLeftAndRight(), 24);
  clockValue_.setBounds(clockInfo_.getRight() + 4, 168, 120, 24);
}

juce::String DasSendEditor::text(const char* japanese, const char* english) const {
  return japanese_ ? juce::String(juce::CharPointer_UTF8(japanese)) : juce::String(english);
}

void DasSendEditor::applyLanguage() {
  languageSwitch_.setJapanese(japanese_);
  syncTitle_.setText(text("タイミング補正", "SYNC"), juce::dontSendNotification);
  manualTitle_.setText(text("手動補正", "MANUAL"), juce::dontSendNotification);
  autoButton_.setButtonText(text("自動補正", "AUTO"));
  timerCallback();
}

void DasSendEditor::darkModeSettingChanged() {
  darkMode_ = resolveDarkMode();
  applyTheme();
}

void DasSendEditor::applyTheme() {
  const auto theme = themeFor(darkMode_);
  title_.setColour(juce::Label::textColourId, theme.ink);
  syncTotal_.setColour(juce::Label::textColourId, theme.ink);
  for (auto* label : {&subtitle_, &obsTitle_, &syncTitle_, &manualTitle_, &clockInfo_,
                      &syncDirection_, &syncBreakdown_, &detail_})
    label->setColour(juce::Label::textColourId, theme.muted);
  languageSwitch_.setColours(theme.accent, theme.muted);

  autoButton_.setColour(juce::ToggleButton::textColourId, theme.ink);
  autoButton_.setColour(juce::ToggleButton::tickColourId, theme.accent);
  autoButton_.setColour(juce::ToggleButton::tickDisabledColourId, theme.muted);
  manualSlider_.setColour(juce::Slider::backgroundColourId, theme.track);
  manualSlider_.setColour(juce::Slider::trackColourId, theme.accent);
  manualSlider_.setColour(juce::Slider::thumbColourId, theme.ink);
  manualSlider_.setColour(juce::Slider::textBoxTextColourId, theme.ink);
  manualSlider_.setColour(juce::Slider::textBoxBackgroundColourId, theme.card);
  manualSlider_.setColour(juce::Slider::textBoxOutlineColourId, theme.line);
  timerCallback();
}

void DasSendEditor::updateStatus(const juce::String& value, const VisualState state) {
  const auto theme = themeFor(darkMode_);
  obsState_ = state;
  obsStatus_.setText(value, juce::dontSendNotification);
  obsStatus_.setColour(juce::Label::textColourId,
                       state == VisualState::active || state == VisualState::error
                           ? juce::Colours::white
                           : state == VisualState::warning ? juce::Colour(0xff2a1a00) : theme.ink);
}

void DasSendEditor::paint(juce::Graphics& graphics) {
  const auto theme = themeFor(darkMode_);
  graphics.fillAll(theme.background);

  auto marker = juce::Path();
  marker.startNewSubPath(24.0F, 47.0F);
  marker.lineTo(137.0F, 43.0F);
  marker.lineTo(141.0F, 58.0F);
  marker.lineTo(22.0F, 61.0F);
  marker.closeSubPath();
  graphics.setColour(theme.accent.withAlpha(0.85F));
  graphics.fillPath(marker);

  drawRaisedPanel(graphics, {24.0F, 82.0F, 278.0F, 124.0F}, 12.0F, theme);
  drawRaisedPanel(graphics, {318.0F, 82.0F, 278.0F, 124.0F}, 12.0F, theme);
  drawRaisedPanel(graphics, {24.0F, 220.0F, 572.0F, 92.0F}, 12.0F, theme);

  const auto statusFill = [&theme](const VisualState state) {
    switch (state) {
      case VisualState::active: return theme.activePill;
      case VisualState::waiting: return theme.idlePill;
      case VisualState::warning: return theme.warning;
      case VisualState::error: return theme.error;
    }
    return theme.idlePill;
  };
  graphics.setColour(statusFill(obsState_));
  graphics.fillRoundedRectangle(106.0F, 120.0F, 168.0F, 32.0F, 16.0F);
  drawWaveIcon(graphics, juce::Rectangle<float>(44.0F, 104.0F, 48.0F, 48.0F),
               colourFor(obsState_));
}

void DasSendEditor::resized() {
  title_.setBounds(26, 18, 250, 40);
  subtitle_.setBounds(380, 26, 112, 22);
  languageSwitch_.setBounds(506, 27, 86, 20);

  obsTitle_.setBounds(108, 94, 168, 22);
  obsStatus_.setBounds(119, 117, 144, 38);
  layoutClock();

  syncTitle_.setBounds(338, 92, 200, 20);
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
    updateStatus(text("1個のみ", "1 ONLY"), VisualState::warning);
    detail_.setText(text("同じユーザーで同時に1個まで", "DAS Kai  x  1 per user"), juce::dontSendNotification);
  } else if (processor_.isRenderingOffline()) {
    updateStatus(text("書き出し中", "EXPORT"), VisualState::waiting);
    detail_.setText(text("書き出し中は送りません", "Not sent while exporting"), juce::dontSendNotification);
  } else if (!processor_.sampleRateSupported()) {
    updateStatus("8-384 kHz", VisualState::error);
    detail_.setText({}, juce::dontSendNotification);
  } else if (!processor_.transportReady()) {
    updateStatus("!", VisualState::error);
    detail_.setText(text("OBS、DAWの順に開き直してください", "RESTART   OBS  >  DAW"), juce::dontSendNotification);
  } else if (processor_.isBypassed()) {
    updateStatus(text("バイパス", "BYPASS"), VisualState::waiting);
    detail_.setText(text("無音を送っています", "Sending silence"), juce::dontSendNotification);
  } else {
    updateStatus(obsActive ? text("受信中", "OK") : text("待機中", "WAIT"),
                 obsActive ? VisualState::active : VisualState::waiting);
    detail_.setText(obsActive ? text("＋で早める　Shift+ドラッグで細かく", "+ = earlier   Shift-drag = fine")
                              : text("OBSのソースに「DAS Kai（DAW）」を追加", "Add source  DAS Kai (DAW)  in OBS"),
                    juce::dontSendNotification);
  }

  // Development override for screenshots of each colour: show this reading.
  const auto* preview = std::getenv("DAS_KAI_UI_PREVIEW_PPM");
  const auto locked = preview != nullptr || processor_.clockLocked();
  const auto drift = preview != nullptr ? std::atof(preview) : processor_.clockDriftPpm();
  const auto ppm = juce::roundToInt(drift);
  const auto caption = locked ? text("クロックのずれ", "CLOCK") : text("クロックを合わせています…", "CLOCK  locking...");
  if (clockInfo_.getText() != caption) {
    clockInfo_.setText(caption, juce::dontSendNotification);
    layoutClock();
  }
  clockValue_.setText(locked ? juce::String(ppm >= 0 ? "+" : "") + juce::String(ppm) + " ppm" : juce::String(),
                      juce::dontSendNotification);
  // Judged on the number shown, so +100 is never yellow.
  clockValue_.setColour(juce::Label::textColourId, clockColour(static_cast<double>(ppm)));

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
    syncDirection_.setText(pending                      ? text("反映中…", "APPLYING...")
                           : std::abs(appliedMs) < 0.05 ? text("補正していません", "NO SHIFT")
                           : appliedMs > 0.0            ? text("DAWの音を早めています", "DAW AUDIO EARLIER")
                                                        : text("DAWの音を遅らせています", "DAW AUDIO LATER"),
                           juce::dontSendNotification);
    syncBreakdown_.setText((autoOn ? text("自動 ", "AUTO ") + formatMs(autoMs, false) : text("自動 オフ", "AUTO off")) +
                               text("　手動 ", "   MANUAL ") + formatMs(manualMs, true),
                           juce::dontSendNotification);
  } else {
    syncTotal_.setText("-- ms", juce::dontSendNotification);
    syncDirection_.setText(text("OBSとつながっていません", "OBS NOT CONNECTED"), juce::dontSendNotification);
    syncBreakdown_.setText(text("手動 ", "MANUAL ") + formatMs(manualMs, true),
                           juce::dontSendNotification);
  }

  repaint();
}
