// SPDX-License-Identifier: AGPL-3.0-only
// Modified for Kinu Stream (2026-11-04): renamed paths and titles.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <cstdlib>

namespace {
class EditorWindow final : public juce::DocumentWindow {
public:
  explicit EditorWindow(juce::AudioPluginInstance& plugin)
      : DocumentWindow("Kinu Stream UI Test", juce::Colour(0xff10151d),
                       DocumentWindow::closeButton) {
    setUsingNativeTitleBar(true);
    setContentOwned(plugin.createEditor(), true);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
  }

  void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

class EditorHostApplication final : public juce::JUCEApplication {
public:
  const juce::String getApplicationName() override { return "Kinu Stream UI Test"; }
  const juce::String getApplicationVersion() override { return "0.1.0"; }

  void initialise(const juce::String&) override {
    setenv("KINU_STREAM_TEST_NAMESPACE", "editor-host", 1);
    auto buildRoot = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                         .getParentDirectory()
                         .getParentDirectory()
                         .getParentDirectory()
                         .getParentDirectory();
    const auto bundle = buildRoot.getChildFile("plugins/send-vst3/KinuStream_artefacts/Release/VST3/Kinu Stream.vst3");
    juce::OwnedArray<juce::PluginDescription> descriptions;
    format_.findAllTypesForFile(descriptions, bundle.getFullPathName());
    if (descriptions.size() != 1) {
      juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                             "Kinu Stream UI Test", "Could not load the VST3");
      return;
    }
    juce::String error;
    plugin_ = format_.createInstanceFromDescription(*descriptions[0], 48000.0, 512, error);
    if (!plugin_) {
      juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                             "Kinu Stream UI Test", error);
      return;
    }
    plugin_->setPlayConfigDetails(2, 2, 48000.0, 512);
    plugin_->prepareToPlay(48000.0, 512);
    window_ = std::make_unique<EditorWindow>(*plugin_);
  }

  void shutdown() override {
    window_.reset();
    if (plugin_) plugin_->releaseResources();
    plugin_.reset();
  }

private:
  juce::VST3PluginFormat format_;
  std::unique_ptr<juce::AudioPluginInstance> plugin_;
  std::unique_ptr<EditorWindow> window_;
};
} // namespace

START_JUCE_APPLICATION(EditorHostApplication)
