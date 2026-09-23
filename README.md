# SoundMeter

A live spectrum analyzer and stereo level meter for everything your computer is playing, written in C++ with [JUCE](https://juce.com).

- **System output capture (macOS 14.2+):** uses a Core Audio *process tap* to capture the mix of all apps, so you don't need a virtual driver like BlackHole.
- **Any input device:** you can also meter a microphone, an interface input, or a loopback driver.
- **Spectrum analyzer:** 64 bands from 20 Hz to 20 kHz on a log scale. Each band has its own color, from red (bass) through the rainbow to violet (treble), with falling peak caps. The display is tilted +3 dB/octave around 1 kHz so typical music reads roughly flat.
- **Level meter:** peak bars (translucent) and RMS bars (solid, 300 ms integration), with a peak-hold marker, a clip LED, and dBFS readouts. Click the meter to reset the hold and clip indicators.

## Requirements

- macOS 14.2 or later, with the Xcode Command Line Tools installed (`xcode-select --install`)
- CMake 3.22 or later (`brew install cmake`)
- The JUCE git submodule: `git submodule update --init`

## Build and run

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
open "build/SoundMeter_artefacts/Release/Sound Meter.app"
```

Launch the app with `open` (or from Finder) so macOS asks for permission for **Sound Meter** itself. The first time it runs, macOS asks for permission to record system audio. If the meters stay flat while audio is playing, turn on Sound Meter under **System Settings › Privacy & Security › Screen & System Audio Recording** (shown as "System Audio Recording Only"), then reopen the app.

## Layout

| File | Purpose |
| --- | --- |
| `Source/Main.cpp` | Application and window |
| `Source/MainComponent.*` | Source selection (system tap or input device) and the audio callback |
| `Source/SpectrumAnalyzer.*` | FFT (4096-point, Hann window), log-spaced bands, and colored bar drawing |
| `Source/LevelMeter.*` | L/R meter drawing and ballistics (60 Hz UI timer) |
| `Source/AudioLevels.h` | Lock-free hand-off of peak/RMS and recent samples from the audio thread to the UI |
| `Source/SystemAudioCapture_mac.mm` | Core Audio process tap and private aggregate device |
| `Source/SystemAudioCapture_stub.cpp` | Placeholder for other platforms (Windows would use WASAPI loopback) |
