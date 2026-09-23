#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "AudioLevels.h"

//==============================================================================
/**
    Stereo peak/RMS meter with peak-hold and clip indicators.

    Polls AudioLevels at 60 Hz and applies meter ballistics on the UI thread.
    Click the meter to clear the peak-hold and clip indicators.
*/
class LevelMeter final : public juce::Component,
                         private juce::Timer
{
public:
    explicit LevelMeter (AudioLevels& source);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    void reset();

    static constexpr float minDb = -60.0f;
    static constexpr float maxDb = 0.0f;

private:
    struct ChannelState
    {
        float peakDb = minDb;       // fast-attack, slow-release peak bar
        float holdDb = minDb;       // peak-hold marker
        double holdAge = 0.0;
        float meanSquare = 0.0f;    // integrated power for the RMS bar
        float lastMeanSquare = 0.0f;
        double sinceData = 0.0;
        bool clipped = false;
    };

    void timerCallback() override;
    void drawChannel (juce::Graphics&, juce::Rectangle<float> bar, const ChannelState&) const;
    void drawScale (juce::Graphics&, juce::Rectangle<float> area, float barTop, float barBottom,
                    juce::Justification) const;

    static float dbToProportion (float db) noexcept;
    static juce::String formatDb (float db);

    AudioLevels& levels;
    std::array<ChannelState, AudioLevels::maxChannels> channels;
    double lastTick = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LevelMeter)
};
