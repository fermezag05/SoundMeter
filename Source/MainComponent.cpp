#include "MainComponent.h"

namespace
{
    const juce::Colour background { 0xff15171c };
    const juce::Colour textDim    { 0xff8a93a3 };
    const juce::Colour textBright { 0xffe6e9ef };
    const juce::Colour errorText  { 0xffff7a7c };
}

//==============================================================================
MainComponent::MainComponent()
{
    title.setText ("Sound Meter", juce::dontSendNotification);
    title.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, textBright);
    addAndMakeVisible (title);

    sourceLabel.setText ("Source", juce::dontSendNotification);
    sourceLabel.setColour (juce::Label::textColourId, textDim);
    addAndMakeVisible (sourceLabel);

    sourceBox.onChange = [this] { sourceChanged(); };
    addAndMakeVisible (sourceBox);

    refreshButton.onClick = [this] { refreshSources(); };
    addAndMakeVisible (refreshButton);

    statusLabel.setFont (juce::FontOptions (12.0f));
    statusLabel.setColour (juce::Label::textColourId, textDim);
    addAndMakeVisible (statusLabel);

    hintLabel.setFont (juce::FontOptions (11.0f));
    hintLabel.setColour (juce::Label::textColourId, textDim.withAlpha (0.8f));
    hintLabel.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (hintLabel);

    addAndMakeVisible (spectrum);
    addAndMakeVisible (meter);

    deviceManager.addAudioCallback (this);

    refreshSources();
    sourceBox.setSelectedId (SystemAudioCapture::isSupported() ? (int) systemOutputId : 0);

    startTimer (500);
    setSize (960, 540);
}

MainComponent::~MainComponent()
{
    stopAll();
    deviceManager.removeAudioCallback (this);
}

//==============================================================================
void MainComponent::refreshSources()
{
    const auto previousText = sourceBox.getText();

    inputNames.clear();

    // Listing devices doesn't open them, so this doesn't trigger any permission prompt.
    for (auto* type : deviceManager.getAvailableDeviceTypes())
    {
        type->scanForDevices();
        inputNames.addArray (type->getDeviceNames (true));
    }

    inputNames.removeDuplicates (false);

    sourceBox.clear (juce::dontSendNotification);

    sourceBox.addSectionHeading ("System");
    sourceBox.addItem ("System output (everything playing)", systemOutputId);
    sourceBox.setItemEnabled (systemOutputId, SystemAudioCapture::isSupported());

    sourceBox.addSectionHeading ("Input devices");
    for (int i = 0; i < inputNames.size(); ++i)
        sourceBox.addItem (inputNames[i], firstInputId + i);

    if (previousText.isNotEmpty())
        for (int i = 0; i < sourceBox.getNumItems(); ++i)
            if (sourceBox.getItemText (i) == previousText)
                sourceBox.setSelectedItemIndex (i, juce::dontSendNotification);
}

void MainComponent::sourceChanged()
{
    const auto id = sourceBox.getSelectedId();

    if (id == systemOutputId)
        startSystemCapture();
    else if (id >= firstInputId)
        startInputDevice (inputNames[id - firstInputId]);
}

void MainComponent::stopAll()
{
    systemCapture.stop();

    if (deviceManagerReady)
        deviceManager.closeAudioDevice();

    levels.reset();
    meter.reset();
    spectrum.reset();
}

void MainComponent::startSystemCapture()
{
    stopAll();

    std::string error;

    if (! systemCapture.start (error))
    {
        setStatus (juce::String (error), true);
        hintLabel.setText ("Choose an input device instead, e.g. a loopback driver such as BlackHole.",
                           juce::dontSendNotification);
        return;
    }

    setStatus ("Capturing system output  ·  "
               + juce::String (systemCapture.getSampleRate() / 1000.0, 1) + " kHz  ·  "
               + juce::String (systemCapture.getNumChannels()) + " ch");

    hintLabel.setText ("Meters flat while audio plays? Allow Sound Meter under System Settings > "
                       "Privacy & Security > Screen & System Audio Recording, then reopen it.",
                       juce::dontSendNotification);
}

void MainComponent::startInputDevice (const juce::String& name)
{
    stopAll();

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    setup.inputDeviceName = name;
    setup.outputDeviceName = {};
    setup.useDefaultInputChannels = false;
    setup.inputChannels.setRange (0, AudioLevels::maxChannels, true);
    setup.useDefaultOutputChannels = false;

    juce::String error;

    if (! deviceManagerReady)
    {
        error = deviceManager.initialise (AudioLevels::maxChannels, 0, nullptr, false, {}, &setup);
        deviceManagerReady = true;
    }
    else
    {
        error = deviceManager.setAudioDeviceSetup (setup, true);
    }

    auto* device = deviceManager.getCurrentAudioDevice();

    if (error.isNotEmpty() || device == nullptr)
    {
        setStatus ("Couldn't open \"" + name + "\": " + (error.isNotEmpty() ? error : "unknown error"), true);
        hintLabel.setText ({}, juce::dontSendNotification);
        return;
    }

    setStatus ("Input: " + device->getName() + "  ·  "
               + juce::String (device->getCurrentSampleRate() / 1000.0, 1) + " kHz  ·  "
               + juce::String (device->getActiveInputChannels().countNumberOfSetBits()) + " ch");

    hintLabel.setText ("Mono inputs are shown on both channels.", juce::dontSendNotification);
}

void MainComponent::setStatus (const juce::String& text, bool isError)
{
    statusLabel.setText (text, juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, isError ? errorText : textDim);
}

void MainComponent::timerCallback()
{
    // Rebuild the tap when the default output changes (e.g. headphones plugged in),
    // since the capture device is clocked from it.
    if (systemCapture.consumeOutputDeviceChanged() && sourceBox.getSelectedId() == systemOutputId)
        startSystemCapture();
}

//==============================================================================
void MainComponent::audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                                      float* const* outputChannelData, int numOutputChannels,
                                                      int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    for (int ch = 0; ch < AudioLevels::maxChannels; ++ch)
    {
        const auto source = juce::jmin (ch, numInputChannels - 1);

        if (source >= 0 && inputChannelData[source] != nullptr)
            levels.pushSamples (ch, inputChannelData[source], numSamples);
    }

    for (int ch = 0; ch < numOutputChannels; ++ch)
        if (outputChannelData[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (background);
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (14);

    title.setBounds (area.removeFromTop (30));
    area.removeFromTop (6);

    auto sourceRow = area.removeFromTop (28).withWidth (juce::jmin (area.getWidth(), 520));
    sourceLabel.setBounds (sourceRow.removeFromLeft (56));
    refreshButton.setBounds (sourceRow.removeFromRight (70));
    sourceRow.removeFromRight (6);
    sourceBox.setBounds (sourceRow);

    area.removeFromTop (4);
    statusLabel.setBounds (area.removeFromTop (22));

    hintLabel.setBounds (area.removeFromBottom (34));
    area.removeFromBottom (6);

    meter.setBounds (area.removeFromRight (130));
    area.removeFromRight (8);
    spectrum.setBounds (area);
}
