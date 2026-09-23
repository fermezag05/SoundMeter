#pragma once

#include <memory>
#include <string>

class AudioLevels;

//==============================================================================
/**
    Captures everything the computer is playing and feeds it into AudioLevels.

    On macOS this uses a Core Audio process tap (macOS 14.2+): a global stereo
    tap of all processes, wrapped in a private aggregate device, so no virtual
    loopback driver is needed. Other platforms currently report unsupported.
*/
class SystemAudioCapture
{
public:
    explicit SystemAudioCapture (AudioLevels& levelsToFeed);
    ~SystemAudioCapture();

    static bool isSupported();

    /** Returns false and fills `error` on failure. */
    bool start (std::string& error);
    void stop();

    bool isRunning() const;
    double getSampleRate() const;
    int getNumChannels() const;

    /** True (once) if the system's default output device changed since the
        last call; the capture should then be restarted to follow it. */
    bool consumeOutputDeviceChanged();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
