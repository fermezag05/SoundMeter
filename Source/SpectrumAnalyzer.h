#pragma once

#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "AudioLevels.h"

//==============================================================================
/**
    Bar-graph spectrum analyzer: log-spaced frequency bands from 20 Hz to 20 kHz
    laid out left to right, each bar rising with that band's level and drawn in
    its own colour (red for bass through to violet for treble).

    Runs an FFT of the latest audio at 60 Hz on the UI thread.
*/
class SpectrumAnalyzer final : public juce::Component,
                               private juce::Timer
{
public:
    explicit SpectrumAnalyzer (AudioLevels& source);

    void paint (juce::Graphics&) override;

    void reset();

    static constexpr int numBands = 64;
    static constexpr float minDb = -80.0f;
    static constexpr float maxDb = 0.0f;
    static constexpr float minFreq = 20.0f;
    static constexpr float maxFreq = 20000.0f;

private:
    static constexpr int fftOrder = 12;
    static constexpr int fftSize = 1 << fftOrder;

    void timerCallback() override;
    void analyse (bool hasNewAudio, double dt);

    static float frequencyToProportion (float freq) noexcept;
    static float dbToProportion (float db) noexcept;
    static juce::Colour colourForBand (int band);

    AudioLevels& levels;

    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann, false };
    std::array<float, fftSize * 2> fftData {};

    std::array<float, numBands> levelDb {};
    std::array<float, numBands> peakDb {};
    std::array<double, numBands> peakAge {};

    uint32_t lastWritten = 0;
    double lastTick = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumAnalyzer)
};
