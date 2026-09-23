#include "SystemAudioCapture.h"
#include "AudioLevels.h"

#import <CoreAudio/AudioHardware.h>
#import <CoreAudio/AudioHardwareTapping.h>
#import <CoreAudio/CATapDescription.h>
#import <Foundation/Foundation.h>

#include <atomic>

#if __has_feature(objc_arc)
 #define SM_RELEASE(x)
#else
 #define SM_RELEASE(x) [x release]
#endif

namespace
{
    template <typename T>
    OSStatus getProperty (AudioObjectID object, AudioObjectPropertySelector selector, T& value)
    {
        const AudioObjectPropertyAddress address { selector,
                                                   kAudioObjectPropertyScopeGlobal,
                                                   kAudioObjectPropertyElementMain };
        UInt32 size = sizeof (T);
        return AudioObjectGetPropertyData (object, &address, 0, nullptr, &size, &value);
    }

    std::string describe (const char* what, OSStatus status)
    {
        // Core Audio errors are usually four-char codes, e.g. '!obj'.
        char code[5] = {};
        bool printable = true;

        for (int i = 0; i < 4; ++i)
        {
            code[i] = (char) ((status >> (24 - 8 * i)) & 0xff);
            printable = printable && code[i] >= 32 && code[i] < 127;
        }

        return std::string (what) + " failed (" + (printable ? "'" + std::string (code) + "'"
                                                             : std::to_string ((int) status)) + ")";
    }

    const AudioObjectPropertyAddress defaultOutputAddress { kAudioHardwarePropertyDefaultOutputDevice,
                                                            kAudioObjectPropertyScopeGlobal,
                                                            kAudioObjectPropertyElementMain };
}

//==============================================================================
struct SystemAudioCapture::Impl
{
    explicit Impl (AudioLevels& l) : levels (l)
    {
        AudioObjectAddPropertyListener (kAudioObjectSystemObject, &defaultOutputAddress, outputChanged, this);
    }

    ~Impl()
    {
        AudioObjectRemovePropertyListener (kAudioObjectSystemObject, &defaultOutputAddress, outputChanged, this);
        stop();
    }

    bool start (std::string& error)
    {
        stop();

        @autoreleasepool
        {
            // 1. A private, unmuted stereo tap of every process on the system.
            CATapDescription* description = [[CATapDescription alloc] initStereoGlobalTapButExcludeProcesses: @[]];
            description.name = @"Sound Meter Tap";
            description.privateTap = YES;
            description.muteBehavior = CATapUnmuted;

            auto status = AudioHardwareCreateProcessTap (description, &tapID);
            SM_RELEASE (description);

            if (status != noErr)
                return fail (error, describe ("Creating the system audio tap", status));

            CFStringRef tapUID = nullptr;
            if ((status = getProperty (tapID, kAudioTapPropertyUID, tapUID)) != noErr)
                return fail (error, describe ("Reading the tap UID", status));

            AudioStreamBasicDescription format {};
            if ((status = getProperty (tapID, kAudioTapPropertyFormat, format)) != noErr)
            {
                CFRelease (tapUID);
                return fail (error, describe ("Reading the tap format", status));
            }

            if ((format.mFormatFlags & kAudioFormatFlagIsFloat) == 0 || format.mBitsPerChannel != 32)
            {
                CFRelease (tapUID);
                return fail (error, "Unexpected tap sample format (expected 32-bit float).");
            }

            sampleRate = format.mSampleRate;
            levels.setSampleRate (sampleRate);
            numChannels = (int) format.mChannelsPerFrame;

            // 2. The tap is read through a private aggregate device, clocked by
            //    the current default output device.
            AudioObjectID outputDevice = kAudioObjectUnknown;
            CFStringRef outputUID = nullptr;

            if ((status = getProperty (kAudioObjectSystemObject, kAudioHardwarePropertyDefaultOutputDevice, outputDevice)) != noErr
                 || (status = getProperty (outputDevice, kAudioDevicePropertyDeviceUID, outputUID)) != noErr)
            {
                CFRelease (tapUID);
                return fail (error, describe ("Finding the default output device", status));
            }

            NSDictionary* aggregate = @{
                @kAudioAggregateDeviceNameKey:          @"Sound Meter Capture",
                @kAudioAggregateDeviceUIDKey:           [[NSUUID UUID] UUIDString],
                @kAudioAggregateDeviceMainSubDeviceKey: (__bridge NSString*) outputUID,
                @kAudioAggregateDeviceIsPrivateKey:     @YES,
                @kAudioAggregateDeviceIsStackedKey:     @NO,
                @kAudioAggregateDeviceTapAutoStartKey:  @YES,
                @kAudioAggregateDeviceSubDeviceListKey: @[ @{ @kAudioSubDeviceUIDKey: (__bridge NSString*) outputUID } ],
                @kAudioAggregateDeviceTapListKey:       @[ @{ @kAudioSubTapDriftCompensationKey: @YES,
                                                              @kAudioSubTapUIDKey: (__bridge NSString*) tapUID } ]
            };

            status = AudioHardwareCreateAggregateDevice ((__bridge CFDictionaryRef) aggregate, &aggregateID);
            CFRelease (tapUID);
            CFRelease (outputUID);

            if (status != noErr)
                return fail (error, describe ("Creating the capture device", status));

            // 3. Receive its input on Core Audio's real-time thread.
            if ((status = AudioDeviceCreateIOProcID (aggregateID, ioProc, this, &procID)) != noErr)
                return fail (error, describe ("Creating the audio callback", status));

            if ((status = AudioDeviceStart (aggregateID, procID)) != noErr)
                return fail (error, describe ("Starting capture", status));
        }

        running = true;
        return true;
    }

    void stop()
    {
        running = false;

        if (aggregateID != kAudioObjectUnknown)
        {
            if (procID != nullptr)
            {
                AudioDeviceStop (aggregateID, procID);
                AudioDeviceDestroyIOProcID (aggregateID, procID);
                procID = nullptr;
            }

            AudioHardwareDestroyAggregateDevice (aggregateID);
            aggregateID = kAudioObjectUnknown;
        }

        if (tapID != kAudioObjectUnknown)
        {
            AudioHardwareDestroyProcessTap (tapID);
            tapID = kAudioObjectUnknown;
        }
    }

    bool fail (std::string& error, std::string message)
    {
        error = std::move (message);
        stop();
        return false;
    }

    static OSStatus ioProc (AudioObjectID, const AudioTimeStamp*, const AudioBufferList* input,
                            const AudioTimeStamp*, AudioBufferList*, const AudioTimeStamp*, void* clientData)
    {
        auto& self = *static_cast<Impl*> (clientData);

        if (input == nullptr)
            return noErr;

        // Buffers may be interleaved or one-per-channel; walk them either way.
        int channel = 0;

        for (UInt32 b = 0; b < input->mNumberBuffers; ++b)
        {
            const auto& buffer = input->mBuffers[b];
            const auto stride = (int) buffer.mNumberChannels;

            if (buffer.mData == nullptr || stride == 0)
                continue;

            const auto* samples = static_cast<const float*> (buffer.mData);
            const auto numFrames = (int) (buffer.mDataByteSize / (sizeof (float) * (UInt32) stride));

            for (int c = 0; c < stride; ++c, ++channel)
                self.levels.pushSamples (channel, samples + c, numFrames, stride);
        }

        return noErr;
    }

    static OSStatus outputChanged (AudioObjectID, UInt32, const AudioObjectPropertyAddress*, void* clientData)
    {
        static_cast<Impl*> (clientData)->outputDeviceChanged = true;
        return noErr;
    }

    AudioLevels& levels;
    AudioObjectID tapID = kAudioObjectUnknown;
    AudioObjectID aggregateID = kAudioObjectUnknown;
    AudioDeviceIOProcID procID = nullptr;
    double sampleRate = 0.0;
    int numChannels = 0;
    bool running = false;
    std::atomic<bool> outputDeviceChanged { false };
};

//==============================================================================
SystemAudioCapture::SystemAudioCapture (AudioLevels& levelsToFeed)
    : impl (std::make_unique<Impl> (levelsToFeed)) {}

SystemAudioCapture::~SystemAudioCapture() = default;

bool SystemAudioCapture::isSupported()
{
    if (@available (macOS 14.2, *))
        return true;

    return false;
}

bool SystemAudioCapture::start (std::string& error)   { return impl->start (error); }
void SystemAudioCapture::stop()                       { impl->stop(); }
bool SystemAudioCapture::isRunning() const            { return impl->running; }
double SystemAudioCapture::getSampleRate() const      { return impl->sampleRate; }
int SystemAudioCapture::getNumChannels() const        { return impl->numChannels; }
bool SystemAudioCapture::consumeOutputDeviceChanged() { return impl->outputDeviceChanged.exchange (false); }
