juce_add_plugin(DasKai
  PRODUCT_NAME "DAS Kai"
  VERSION "${DAS_COMPONENT_VERSION}"
  COMPANY_NAME "${DAS_PUBLISHER}"
  BUNDLE_ID "com.keinoaza.kinustream"
  PLUGIN_MANUFACTURER_CODE Kein
  PLUGIN_CODE Knst
  FORMATS VST3 AU
  VST3_CATEGORIES Fx Tools
  AU_MAIN_TYPE kAudioUnitType_Effect
  VST3_AUTO_MANIFEST FALSE
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE
)

target_sources(DasKai PRIVATE
  Source/PluginEditor.cpp
  Source/PluginEditor.h
  Source/PluginProcessor.cpp
  Source/PluginProcessor.h
)
target_compile_definitions(DasKai PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1
)
target_link_libraries(DasKai PRIVATE
  das_audio_core
  das_sync
  das_transport
  juce::juce_audio_utils
  juce::juce_audio_processors
)
