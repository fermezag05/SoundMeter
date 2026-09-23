#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioLevels.h"
#include "LevelMeter.h"
#include "SpectrumAnalyzer.h"
#include "SystemAudioCapture.h"

//==============================================================================
/**
    Hosts the meter and lets the user choose what it measures: the computer's
    whole output (system tap) or any audio input device, such as a microphone
    or a loopback driver like BlackHole.
*/
class MainComponent final : public juce::Component,
                            private juce::AudioIODeviceCallback,
                            private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    enum SourceIds
    {
        systemOutputId = 1,
        firstInputId = 100
    };

    void refreshSources();
    void sourceChanged();
    void startSystemCapture();
    void startInputDevice (const juce::String& name);
    void stopAll();
    void setStatus (const juce::String& text, bool isError = false);

    void timerCallback() override;

    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override { levels.setSampleRate (device->getCurrentSampleRate()); }
    void audioDeviceStopped() override {}

    AudioLevels levels;
    SystemAudioCapture systemCapture { levels };
    juce::AudioDeviceManager deviceManager;
    bool deviceManagerReady = false;
    juce::StringArray inputNames;

    juce::Label title, sourceLabel, statusLabel, hintLabel;
    juce::ComboBox sourceBox;
    juce::TextButton refreshButton { "Refresh" };
    SpectrumAnalyzer spectrum { levels };
    LevelMeter meter { levels };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
