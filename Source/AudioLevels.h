#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

//==============================================================================
/**
    Lock-free hand-off of audio from an audio thread to the UI.

    The audio thread calls pushSamples() for every block it receives. The UI can
    then:
      - call take() for the peak and RMS of everything pushed since the last call;
      - call copyLatestMono() for the most recent raw samples (for the spectrum).
*/
class AudioLevels
{
public:
    static constexpr int maxChannels = 2;
    static constexpr int historySize = 8192;   // power of two

    struct Reading
    {
        float peak = 0.0f;          // linear, 0..1 (can exceed 1 when clipping)
        float meanSquare = 0.0f;    // linear power
        int numSamples = 0;         // 0 means no audio arrived since last take()
    };

    /** Real-time safe. `stride` lets interleaved buffers be read in place. */
    void pushSamples (int channel, const float* data, int numFrames, int stride = 1) noexcept
    {
        if (channel < 0 || channel >= maxChannels || data == nullptr || numFrames <= 0)
            return;

        auto& ch = channels[(size_t) channel];
        const auto writePos = ch.written.load (std::memory_order_relaxed);

        float peak = 0.0f;
        double sumSquares = 0.0;

        for (int i = 0; i < numFrames; ++i)
        {
            const float s = data[i * stride];
            peak = std::max (peak, std::abs (s));
            sumSquares += (double) s * s;
            ch.history[(writePos + (uint32_t) i) & historyMask].store (s, std::memory_order_relaxed);
        }

        ch.written.store (writePos + (uint32_t) numFrames, std::memory_order_release);

        auto prevPeak = ch.peak.load (std::memory_order_relaxed);
        while (peak > prevPeak && ! ch.peak.compare_exchange_weak (prevPeak, peak, std::memory_order_relaxed)) {}

        auto prevSum = ch.sumSquares.load (std::memory_order_relaxed);
        while (! ch.sumSquares.compare_exchange_weak (prevSum, prevSum + sumSquares, std::memory_order_relaxed)) {}

        ch.numSamples.fetch_add (numFrames, std::memory_order_relaxed);
    }

    Reading take (int channel) noexcept
    {
        auto& ch = channels[(size_t) channel];

        Reading r;
        r.peak = ch.peak.exchange (0.0f, std::memory_order_relaxed);
        const auto sum = ch.sumSquares.exchange (0.0, std::memory_order_relaxed);
        r.numSamples = ch.numSamples.exchange (0, std::memory_order_relaxed);
        r.meanSquare = r.numSamples > 0 ? (float) (sum / r.numSamples) : 0.0f;
        return r;
    }

    /** Fills `dest` with the latest `numSamples` (<= historySize) as a mono mix.
        Returns the running count of samples written, so callers can tell
        whether any new audio has arrived. */
    uint32_t copyLatestMono (float* dest, int numSamples) const noexcept
    {
        std::fill (dest, dest + numSamples, 0.0f);

        for (const auto& ch : channels)
        {
            const auto start = ch.written.load (std::memory_order_acquire) - (uint32_t) numSamples;

            for (int i = 0; i < numSamples; ++i)
                dest[i] += ch.history[(start + (uint32_t) i) & historyMask].load (std::memory_order_relaxed)
                             / (float) maxChannels;
        }

        return channels[0].written.load (std::memory_order_relaxed);
    }

    void setSampleRate (double newRate) noexcept    { if (newRate > 0.0) sampleRate = newRate; }
    double getSampleRate() const noexcept           { return sampleRate; }

    void reset() noexcept
    {
        for (int i = 0; i < maxChannels; ++i)
            take (i);
    }

private:
    static constexpr uint32_t historyMask = historySize - 1;

    struct Channel
    {
        std::atomic<float> peak { 0.0f };
        std::atomic<double> sumSquares { 0.0 };
        std::atomic<int> numSamples { 0 };
        std::atomic<uint32_t> written { 0 };
        std::array<std::atomic<float>, historySize> history {};
    };

    std::array<Channel, maxChannels> channels;
    std::atomic<double> sampleRate { 48000.0 };
};
