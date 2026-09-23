#include "SystemAudioCapture.h"

// Fallback for platforms without a system-output capture implementation yet
// (a Windows version would use WASAPI loopback capture).

struct SystemAudioCapture::Impl {};

SystemAudioCapture::SystemAudioCapture (AudioLevels&) {}
SystemAudioCapture::~SystemAudioCapture() = default;

bool SystemAudioCapture::isSupported()                  { return false; }
bool SystemAudioCapture::start (std::string& error)     { error = "System audio capture is not implemented on this platform."; return false; }
void SystemAudioCapture::stop()                         {}
bool SystemAudioCapture::isRunning() const              { return false; }
double SystemAudioCapture::getSampleRate() const        { return 0.0; }
int SystemAudioCapture::getNumChannels() const          { return 0; }
bool SystemAudioCapture::consumeOutputDeviceChanged()   { return false; }
