#pragma once

// ProcessTap: uses macOS 14.2+ AudioHardwareCreateProcessTap to intercept
// audio output from a specific process (djay Pro AI) to a specific device
// stream (FLX4 output stream 1 = cue channels 3-4).
//
// Architecture (per Apple SDK requirements):
//   1. CATapDescription → AudioHardwareCreateProcessTap → tapID (AudioObject, NOT a device)
//   2. Create a tap-only aggregate device referencing the tap's UUID
//   3. IOProc on the aggregate device reads tapped audio from inInputData
//
// The tap does NOT have its own IO path. Audio is only accessible through
// an aggregate device that contains the tap (AudioHardware.h: "AudioSubTap
// objects do not implement an IO path of their own").
//
// CATapUnmuted = audio still plays on the FLX4 headphone jack too.

#include <CoreAudio/CoreAudio.h>
#include <functional>
#include <string>

namespace flux {

class ProcessTap {
public:
    using TapCallback = std::function<void(
        const AudioBufferList* inputData,
        const AudioTimeStamp*  inputTime,
        UInt32                 frameCount
    )>;

    ProcessTap() = default;
    ~ProcessTap();

    // Create a tap on a specific output stream of a device, filtering to
    // a specific process. streamIndex is 0-based (stream 0 = output 1-2,
    // stream 1 = output 3-4 on FLX4).
    // If processName is empty, taps ALL processes on that stream.
    bool create(const std::string& deviceUID,
                int streamIndex,
                const std::string& processName);

    // Start reading from the tap. Callback fires on the aggregate's IO thread.
    bool start(TapCallback callback);

    // Stop and destroy.
    void stop();

    bool isRunning() const { return running_; }
    AudioObjectID tapID() const { return tapID_; }

private:
    // The tap object — not a device, just an AudioObject with streams.
    AudioObjectID       tapID_ = kAudioObjectUnknown;

    // Aggregate device wrapping the tap — this IS the device we open IOProc on.
    AudioDeviceID       aggregateDeviceID_ = kAudioObjectUnknown;

    AudioDeviceIOProcID ioProcID_ = nullptr;
    TapCallback         callback_;
    bool                running_ = false;

    // Creates a private aggregate device containing only the tap.
    bool createAggregateDevice();

    static OSStatus staticIOProc(
        AudioObjectID           inDevice,
        const AudioTimeStamp*   inNow,
        const AudioBufferList*  inInputData,
        const AudioTimeStamp*   inInputTime,
        AudioBufferList*        outOutputData,
        const AudioTimeStamp*   outOutputTime,
        void*                   inClientData);
};

} // namespace flux
