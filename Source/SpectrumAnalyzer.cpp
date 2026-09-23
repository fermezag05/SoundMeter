#include "SpectrumAnalyzer.h"

namespace
{
    // Ballistics
    constexpr double fallDbPerSecond = 40.0;
    constexpr double peakHoldSeconds = 0.8;
    constexpr double peakFallDbPerSecond = 25.0;

    // Music has roughly a pink (-3 dB/octave) spectrum; tilting by the opposite
    // amount around 1 kHz makes it read flat, like most hardware analyzers.
    constexpr float tiltDbPerOctave = 3.0f;
    constexpr float tiltReferenceHz = 1000.0f;

    const juce::Colour background { 0xff15171c };
    const juce::Colour plotBackground { 0xff1a1d23 };
    const juce::Colour gridLine { 0xff262a32 };
    const juce::Colour textDim { 0xff8a93a3 };

    const float frequencyLabels[] = { 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
}

//==============================================================================
SpectrumAnalyzer::SpectrumAnalyzer (AudioLevels& source) : levels (source)
{
    setOpaque (true);
    reset();
    lastTick = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (60);
}

void SpectrumAnalyzer::reset()
{
    levelDb.fill (minDb);
    peakDb.fill (minDb);
    peakAge.fill (0.0);
    repaint();
}

float SpectrumAnalyzer::frequencyToProportion (float freq) noexcept
{
    return std::log (freq / minFreq) / std::log (maxFreq / minFreq);
}

float SpectrumAnalyzer::dbToProportion (float db) noexcept
{
    return juce::jlimit (0.0f, 1.0f, (db - minDb) / (maxDb - minDb));
}

juce::Colour SpectrumAnalyzer::colourForBand (int band)
{
    // Red for the lowest band through the rainbow to violet for the highest.
    const auto hue = 0.83f * (float) band / (float) (numBands - 1);
    return juce::Colour::fromHSV (hue, 0.85f, 1.0f, 1.0f);
}

//==============================================================================
void SpectrumAnalyzer::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto dt = juce::jlimit (0.0, 0.25, (now - lastTick) * 0.001);
    lastTick = now;

    const auto written = levels.copyLatestMono (fftData.data(), fftSize);
    const bool hasNewAudio = written != lastWritten;
    lastWritten = written;

    analyse (hasNewAudio, dt);
    repaint();
}

void SpectrumAnalyzer::analyse (bool hasNewAudio, double dt)
{
    std::array<float, numBands> targetDb;
    targetDb.fill (minDb);

    if (hasNewAudio)
    {
        window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
        fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

        // A full-scale sine through a Hann window peaks at fftSize / 4.
        const auto normalise = 4.0f / (float) fftSize;
        const auto binsPerHz = (float) fftSize / (float) levels.getSampleRate();
        const auto lastBin = (float) (fftSize / 2);

        const auto magnitudeAt = [&] (float bin)
        {
            bin = juce::jlimit (0.0f, lastBin, bin);
            const auto i = (int) bin;
            const auto frac = bin - (float) i;
            return fftData[(size_t) i] + frac * (fftData[(size_t) juce::jmin (i + 1, fftSize / 2)] - fftData[(size_t) i]);
        };

        for (int b = 0; b < numBands; ++b)
        {
            const auto lowHz = minFreq * std::pow (maxFreq / minFreq, (float) b / numBands);
            const auto highHz = minFreq * std::pow (maxFreq / minFreq, (float) (b + 1) / numBands);
            const auto centreHz = std::sqrt (lowHz * highHz);

            const auto firstBin = (int) std::ceil (lowHz * binsPerHz);
            const auto endBin = juce::jmin ((int) std::floor (highHz * binsPerHz), fftSize / 2);

            // Narrow (bass) bands fall between FFT bins, so interpolate;
            // wider bands take the loudest bin they cover.
            float magnitude = magnitudeAt (centreHz * binsPerHz);

            for (int bin = firstBin; bin <= endBin; ++bin)
                magnitude = juce::jmax (magnitude, fftData[(size_t) bin]);

            const auto tilt = tiltDbPerOctave * std::log2 (centreHz / tiltReferenceHz);
            targetDb[(size_t) b] = juce::Decibels::gainToDecibels (magnitude * normalise, minDb) + tilt;
        }
    }

    const auto fall = (float) (fallDbPerSecond * dt);
    const auto peakFall = (float) (peakFallDbPerSecond * dt);

    for (size_t b = 0; b < (size_t) numBands; ++b)
    {
        levelDb[b] = juce::jlimit (minDb, maxDb, juce::jmax (targetDb[b], levelDb[b] - fall));

        if (levelDb[b] >= peakDb[b])
        {
            peakDb[b] = levelDb[b];
            peakAge[b] = 0.0;
        }
        else if ((peakAge[b] += dt) > peakHoldSeconds)
        {
            peakDb[b] = juce::jmax (levelDb[b], peakDb[b] - peakFall);
        }
    }
}

//==============================================================================
void SpectrumAnalyzer::paint (juce::Graphics& g)
{
    g.fillAll (background);

    auto area = getLocalBounds().toFloat().reduced (10.0f);
    auto axisLeft = area.removeFromLeft (30.0f);
    auto axisBottom = area.removeFromBottom (18.0f);
    const auto plot = area;

    g.setColour (plotBackground);
    g.fillRoundedRectangle (plot, 4.0f);

    // dB grid
    g.setFont (juce::FontOptions (10.0f));

    for (auto db = maxDb; db >= minDb; db -= 10.0f)
    {
        const auto y = plot.getBottom() - dbToProportion (db) * plot.getHeight();

        g.setColour (gridLine);
        g.fillRect (plot.getX(), y - 0.5f, plot.getWidth(), 1.0f);

        g.setColour (textDim);
        g.drawText (juce::String ((int) db), axisLeft.withY (y - 6.0f).withHeight (12.0f).withTrimmedRight (6.0f),
                    juce::Justification::centredRight);
    }

    // Frequency labels
    for (auto freq : frequencyLabels)
    {
        const auto x = plot.getX() + frequencyToProportion (freq) * plot.getWidth();
        const auto text = freq >= 1000.0f ? juce::String ((int) (freq / 1000.0f)) + "k" : juce::String ((int) freq);

        g.setColour (textDim);
        g.drawText (text, juce::Rectangle<float> (x - 20.0f, axisBottom.getY() + 2.0f, 40.0f, 14.0f),
                    juce::Justification::centred);
    }

    // Bars
    const auto slot = plot.getWidth() / (float) numBands;
    const auto gap = juce::jmax (1.0f, slot * 0.2f);

    for (int b = 0; b < numBands; ++b)
    {
        const auto colour = colourForBand (b);
        const auto x = plot.getX() + (float) b * slot + gap * 0.5f;
        const auto width = slot - gap;

        const auto top = plot.getBottom() - dbToProportion (levelDb[(size_t) b]) * plot.getHeight();

        if (top < plot.getBottom())
        {
            g.setGradientFill (juce::ColourGradient (colour.darker (0.8f), x, plot.getBottom(),
                                                     colour, x, plot.getY(), false));
            g.fillRect (juce::Rectangle<float> (x, top, width, plot.getBottom() - top));

            // Segment the bar into LEDs.
            g.setColour (plotBackground);
            for (auto y = plot.getBottom() - 4.0f; y > top; y -= 4.0f)
                g.fillRect (x, y, width, 1.0f);
        }

        if (peakDb[(size_t) b] > minDb)
        {
            const auto peakY = plot.getBottom() - dbToProportion (peakDb[(size_t) b]) * plot.getHeight();
            g.setColour (colour.brighter (0.5f));
            g.fillRect (x, peakY - 1.0f, width, 2.0f);
        }
    }
}
