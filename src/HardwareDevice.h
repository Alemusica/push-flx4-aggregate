#pragma once

// HardwareDevice wraps the client HAL API to open IOProcs on real USB audio
// devices from within the plugin. This violates Apple's documented contract
// (AudioServerPlugIn.h forbids client HAL calls from plugins) but works
// reliably in practice across macOS 11–15 (see proxy-audio-device).
//
// For Architecture A (IPC companion), this code would move to the helper
// process and communicate via Mach shared memory instead.

#include <CoreAudio/CoreAudio.h>
#include <functional>
#include <string>

class HardwareDevice {
public:
    // IOProc callback signature: (inDevice, hostTime, inputData, inputTime,
    //                             outputData, outputTime, userData)
    using IOCallback = std::function<void(
        AudioDeviceID,
        const AudioTimeStamp*,
        const AudioBufferList*,
        const AudioTimeStamp*,
        AudioBufferList*,
        const AudioTimeStamp*
    )>;

    HardwareDevice() = default;
    ~HardwareDevice();

    // Find and bind to a device by its UID string.
    // Returns false if the device is not found.
    bool open(const std::string& deviceUID);

    // Start the IOProc. Callback fires on the device's realtime thread.
    bool start(IOCallback callback);

    // Stop and destroy the IOProc.
    void stop();

    bool isRunning() const { return running_; }
    AudioDeviceID deviceID() const { return deviceID_; }

    // Query the device's current nominal sample rate.
    double nominalSampleRate() const;

    // Query the device's hardware latency (frames).
    UInt32 deviceLatency(bool input) const;
    UInt32 safetyOffset(bool input) const;
    UInt32 bufferFrameSize() const;

private:
    static OSStatus staticIOProc(
        AudioObjectID           inDevice,
        const AudioTimeStamp*   inNow,
        const AudioBufferList*  inInputData,
        const AudioTimeStamp*   inInputTime,
        AudioBufferList*        outOutputData,
        const AudioTimeStamp*   outOutputTime,
        void*                   inClientData);

    AudioDeviceID       deviceID_ = kAudioObjectUnknown;
    AudioDeviceIOProcID ioProcID_ = nullptr;
    IOCallback          callback_;
    bool                running_ = false;
};
