#pragma once

// AudioEngine: the core of the helper daemon.
//
// Manages both hardware devices (Push = master, FLX4 = slave).
// Runs DriftTrackers on both, feeds the adaptive resampler for FLX4,
// and writes all audio + clock data into shared memory for the plugin.

#include "HardwareDevice.h"
#include "ProcessTap.h"
#include "SharedMemory.h"
#include "DriftTracker.h"

#include <samplerate.h>
#include <string>

namespace flux {

class AudioEngine {
public:
    AudioEngine(SharedMemoryLayout* shm,
                const std::string& pushUID,
                const std::string& flx4UID);
    ~AudioEngine();

    // Open devices and start IOProcs. Non-blocking — callbacks run on
    // CoreAudio's realtime threads.
    bool start();

    // Stop IOProcs and release devices.
    void stop();

    bool isRunning() const { return running_; }

private:
    // IOProc callbacks — called on CoreAudio's realtime threads.
    void onPushIO(
        AudioDeviceID device,
        const AudioTimeStamp* now,
        const AudioBufferList* inputData,
        const AudioTimeStamp* inputTime,
        AudioBufferList* outputData,
        const AudioTimeStamp* outputTime);

    void onFLX4IO(
        AudioDeviceID device,
        const AudioTimeStamp* now,
        const AudioBufferList* inputData,
        const AudioTimeStamp* inputTime,
        AudioBufferList* outputData,
        const AudioTimeStamp* outputTime);

    SharedMemoryLayout* shm_;
    std::string pushUID_;
    std::string flx4UID_;

    HardwareDevice pushHW_;
    HardwareDevice flx4HW_;

    DriftTracker pushDLL_{48000.0};   // Push 3 native rate
    DriftTracker flx4DLL_{48000.0};   // FLX4 supports 44100+48000, use 48k to match Push

    // Resamplers for FLX4 slave path (stereo).
    // Input resampler: FLX4 hardware → shared memory (FLX4→Push clock domain).
    // Output resampler: shared memory → FLX4 hardware (Push→FLX4 clock domain).
    // Cue resampler: tap audio → shared memory (FLX4→Push clock domain).
    SRC_STATE* resamplerIn_  = nullptr;
    SRC_STATE* resamplerOut_ = nullptr;
    SRC_STATE* resamplerCue_ = nullptr;

    // Process tap for FLX4 cue output (djay → FLX4 stream 1 = channels 3-4).
    ProcessTap cueTap_;

    // Intermediate buffer for resampler output.
    static constexpr int kResampleBufFrames = 4096;
    float resampleBuf_[kResampleBufFrames * kChannelsPerDevice] = {};
    float cueResampleBuf_[kResampleBufFrames * kChannelsPerDevice] = {};

    bool running_ = false;
};

} // namespace flux
