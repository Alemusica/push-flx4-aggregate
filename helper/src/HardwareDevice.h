#pragma once

// Wrapper around CoreAudio client HAL API to open IOProcs on real USB
// audio devices. This runs in the helper daemon — OUTSIDE the coreaudiod
// sandbox — so all client HAL calls are legal and Apple-sanctioned.

#include <CoreAudio/CoreAudio.h>
#include <functional>
#include <string>

namespace flux {

class HardwareDevice {
public:
    using IOCallback = std::function<void(
        AudioDeviceID           device,
        const AudioTimeStamp*   now,
        const AudioBufferList*  inputData,
        const AudioTimeStamp*   inputTime,
        AudioBufferList*        outputData,
        const AudioTimeStamp*   outputTime
    )>;

    HardwareDevice() = default;
    ~HardwareDevice();

    // Non-copyable, movable.
    HardwareDevice(const HardwareDevice&) = delete;
    HardwareDevice& operator=(const HardwareDevice&) = delete;

    // Find device by UID string. Returns false if not found.
    bool open(const std::string& deviceUID);

    // Start the IOProc. Callback fires on the device's realtime thread.
    bool start(IOCallback callback);

    // Stop and destroy the IOProc.
    void stop();

    bool isRunning() const { return running_; }
    AudioDeviceID deviceID() const { return deviceID_; }
    const std::string& uid() const { return uid_; }

    double nominalSampleRate() const;
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
    std::string         uid_;
    bool                running_ = false;
};

} // namespace flux
