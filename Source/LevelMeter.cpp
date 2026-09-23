#include "LevelMeter.h"

namespace
{
    // Ballistics
    constexpr double peakFallDbPerSecond = 20.0;
    constexpr double holdSeconds = 1.5;
    constexpr double holdFallDbPerSecond = 15.0;
    constexpr double rmsIntegrationSeconds = 0.3;
    constexpr double dataTimeoutSeconds = 0.1;
    constexpr float clipThreshold = 0.999f;

    // Colours
    const juce::Colour background   { 0xff15171c };
    const juce::Colour barTrack     { 0xff1f232a };
    const juce::Colour textDim      { 0xff8a93a3 };
    const juce::Colour textBright   { 0xffe6e9ef };
    const juce::Colour green        { 0xff2fd67b };
    const juce::Colour yellow       { 0xfff2c94c };
    const juce::Colour red          { 0xffff4d4f };

    const float scaleTicks[] = { 0, -3, -6, -9, -12, -18, -24, -30, -40, -50, -60 };

    juce::Colour colourForDb (float db)
    {
        if (db >= -6.0f)  return red;
        if (db >= -18.0f) return yellow;
        return green;
    }
}

//==============================================================================
LevelMeter::LevelMeter (AudioLevels& source) : levels (source)
{
    setOpaque (true);
    lastTick = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (60);
}

void LevelMeter::reset()
{
    channels = {};
    repaint();
}

void LevelMeter::mouseDown (const juce::MouseEvent&)
{
    for (auto& ch : channels)
    {
        ch.clipped = false;
        ch.holdDb = ch.peakDb;
        ch.holdAge = 0.0;
    }

    repaint();
}

void LevelMeter::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto dt = juce::jlimit (0.0, 0.25, (now - lastTick) * 0.001);
    lastTick = now;

    const auto rmsAlpha = (float) (1.0 - std::exp (-dt / rmsIntegrationSeconds));

    for (int i = 0; i < AudioLevels::maxChannels; ++i)
    {
        auto& ch = channels[(size_t) i];
        const auto reading = levels.take (i);

        // Audio blocks and UI frames don't line up, so briefly reuse the last
        // block's power when a frame received nothing, rather than dipping.
        float meanSquare = 0.0f;

        if (reading.numSamples > 0)
        {
            meanSquare = ch.lastMeanSquare = reading.meanSquare;
            ch.sinceData = 0.0;
        }
        else
        {
            ch.sinceData += dt;
            meanSquare = ch.sinceData < dataTimeoutSeconds ? ch.lastMeanSquare : 0.0f;
        }

        ch.meanSquare += (meanSquare - ch.meanSquare) * rmsAlpha;

        const auto newPeakDb = juce::Decibels::gainToDecibels (reading.peak, minDb);
        ch.peakDb = juce::jmax (newPeakDb, ch.peakDb - (float) (peakFallDbPerSecond * dt), minDb);

        if (newPeakDb >= ch.holdDb)
        {
            ch.holdDb = newPeakDb;
            ch.holdAge = 0.0;
        }
        else
        {
            ch.holdAge += dt;

            if (ch.holdAge > holdSeconds)
                ch.holdDb = juce::jmax (ch.peakDb, ch.holdDb - (float) (holdFallDbPerSecond * dt));
        }

        if (reading.peak >= clipThreshold)
            ch.clipped = true;
    }

    repaint();
}

//==============================================================================
float LevelMeter::dbToProportion (float db) noexcept
{
    return juce::jlimit (0.0f, 1.0f, (db - minDb) / (maxDb - minDb));
}

juce::String LevelMeter::formatDb (float db)
{
    return db <= minDb ? juce::String ("-inf") : juce::String (db, 1);
}

void LevelMeter::paint (juce::Graphics& g)
{
    g.fillAll (background);

    auto area = getLocalBounds().toFloat().reduced (10.0f);

    auto readoutRow = area.removeFromTop (20.0f);
    area.removeFromTop (4.0f);
    auto clipRow = area.removeFromTop (8.0f);
    area.removeFromTop (6.0f);
    auto labelRow = area.removeFromBottom (38.0f);

    constexpr float scaleWidth = 30.0f, gap = 14.0f;
    auto leftScale = area.removeFromLeft (scaleWidth);
    auto rightScale = area.removeFromRight (scaleWidth);
    const auto barWidth = (area.getWidth() - gap) * 0.5f;

    const std::array<juce::Rectangle<float>, 2> bars { area.withWidth (barWidth),
                                                        area.withTrimmedLeft (barWidth + gap) };

    drawScale (g, leftScale, area.getY(), area.getBottom(), juce::Justification::centredRight);
    drawScale (g, rightScale, area.getY(), area.getBottom(), juce::Justification::centredLeft);

    const char* names[] = { "L", "R" };

    for (size_t i = 0; i < bars.size(); ++i)
    {
        const auto& ch = channels[i];
        const auto& bar = bars[i];

        drawChannel (g, bar, ch);

        // Peak-hold readout
        g.setColour (ch.holdDb > -6.0f ? colourForDb (ch.holdDb) : textBright);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText (formatDb (ch.holdDb), readoutRow.withX (bar.getX()).withWidth (bar.getWidth()),
                    juce::Justification::centred);

        // Clip LED
        auto led = clipRow.withX (bar.getX()).withWidth (bar.getWidth());
        g.setColour (ch.clipped ? red : barTrack);
        g.fillRoundedRectangle (led, 2.0f);

        // Channel name and RMS
        auto label = labelRow.withX (bar.getX()).withWidth (bar.getWidth());
        g.setColour (textBright);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText (names[i], label.removeFromTop (18.0f), juce::Justification::centred);

        g.setColour (textDim);
        g.setFont (juce::FontOptions (11.0f));
        const auto rmsDb = juce::Decibels::gainToDecibels (std::sqrt (ch.meanSquare), minDb);
        g.drawText ("RMS " + formatDb (rmsDb), label, juce::Justification::centred);
    }
}

void LevelMeter::drawChannel (juce::Graphics& g, juce::Rectangle<float> bar, const ChannelState& ch) const
{
    g.setColour (barTrack);
    g.fillRoundedRectangle (bar, 3.0f);

    juce::ColourGradient gradient (green, bar.getBottomLeft(), red, bar.getTopLeft(), false);
    gradient.addColour (dbToProportion (-18.0f), yellow.interpolatedWith (green, 0.5f));
    gradient.addColour (dbToProportion (-6.0f), yellow);
    gradient.addColour (dbToProportion (-3.0f), red);

    const auto yFor = [&] (float db) { return bar.getBottom() - dbToProportion (db) * bar.getHeight(); };

    const auto rmsDb = juce::Decibels::gainToDecibels (std::sqrt (ch.meanSquare), minDb);
    const auto rmsY = yFor (rmsDb);
    const auto peakY = yFor (ch.peakDb);

    // Peak as a translucent bar, RMS as the solid body beneath it.
    g.setGradientFill (gradient);
    g.setOpacity (0.35f);
    g.fillRect (bar.withTop (peakY));
    g.setOpacity (1.0f);
    g.fillRect (bar.withTop (rmsY));

    // Segment the bar into LEDs.
    g.setColour (barTrack);
    for (float y = bar.getBottom() - 4.0f; y > bar.getY(); y -= 4.0f)
        g.fillRect (bar.getX(), y, bar.getWidth(), 1.0f);

    // Peak-hold marker
    if (ch.holdDb > minDb)
    {
        g.setColour (colourForDb (ch.holdDb));
        g.fillRect (bar.getX(), yFor (ch.holdDb) - 1.0f, bar.getWidth(), 2.0f);
    }
}

void LevelMeter::drawScale (juce::Graphics& g, juce::Rectangle<float> area, float barTop, float barBottom,
                            juce::Justification justification) const
{
    g.setFont (juce::FontOptions (10.0f));

    const bool alignRight = justification.testFlags (juce::Justification::right);

    for (auto db : scaleTicks)
    {
        const auto y = barBottom - dbToProportion (db) * (barBottom - barTop);

        g.setColour (textDim.withAlpha (0.5f));
        g.fillRect (alignRight ? area.getRight() - 4.0f : area.getX(), y - 0.5f, 4.0f, 1.0f);

        g.setColour (textDim);
        auto textArea = juce::Rectangle<float> (area.getX(), y - 6.0f, area.getWidth(), 12.0f)
                            .reduced (6.0f, 0.0f);
        g.drawText (juce::String ((int) db), textArea, justification);
    }
}
