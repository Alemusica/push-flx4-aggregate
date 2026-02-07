#pragma once

// AggregateHandler implements the realtime IO path and lifecycle control.
// It opens IOProcs on both USB devices, manages ring buffers, runs the
// drift DLL, and feeds the adaptive resampler for the slave (FLX4) path.

#include <aspl/ControlRequestHandler.hpp>
#include <aspl/IORequestHandler.hpp>
#include <aspl/Stream.hpp>
#include <memory>
#include <samplerate.h>

#include "AggregateDevice.h"
#include "DriftTracker.h"
#include "HardwareDevice.h"
#include "TPCircularBuffer.h"

class AggregateHandler : public aspl::ControlRequestHandler,
                         public aspl::IORequestHandler {
public:
    AggregateHandler(
        std::shared_ptr<AggregateDevice> device,
        std::shared_ptr<aspl::Stream> pushIn,
        std::shared_ptr<aspl::Stream> pushOut,
        std::shared_ptr<aspl::Stream> flx4In,
        std::shared_ptr<aspl::Stream> flx4Out);

    ~AggregateHandler() override;

    // -- ControlRequestHandler --
    OSStatus OnStartIO() override;
    void     OnStopIO() override;

    // -- IORequestHandler --
    void OnReadClientInput(
        const std::shared_ptr<aspl::Client>& client,
        const std::shared_ptr<aspl::Stream>& stream,
        Float64 zeroTimestamp,
        Float64 timestamp,
        void*   buff,
        UInt32  buffBytesSize) override;

    void OnWriteMixedOutput(
        const std::shared_ptr<aspl::Stream>& stream,
        Float64 zeroTimestamp,
        Float64 timestamp,
        const void* buff,
        UInt32 buffBytesSize) override;

private:
    // Hardware device UIDs — TODO: make configurable
    static constexpr const char* kPushUID  = "AppleUSBAudioEngine:Ableton:Push 2:PLACEHOLDER";
    static constexpr const char* kFLX4UID  = "AppleUSBAudioEngine:Pioneer:DDJ-FLX4:PLACEHOLDER";

    // Ring buffer capacity (~50ms at 44100 Hz, stereo float32)
    static constexpr int32_t kRingBufferBytes = 65536;

    // Resampler intermediate buffer size
    static constexpr int kResampleBufFrames = 4096;

    void pushIOCallback(
        AudioDeviceID device,
        const AudioTimeStamp* now,
        const AudioBufferList* inputData,
        const AudioTimeStamp* inputTime,
        AudioBufferList* outputData,
        const AudioTimeStamp* outputTime);

    void flx4IOCallback(
        AudioDeviceID device,
        const AudioTimeStamp* now,
        const AudioBufferList* inputData,
        const AudioTimeStamp* inputTime,
        AudioBufferList* outputData,
        const AudioTimeStamp* outputTime);

    // Virtual device and streams
    std::shared_ptr<AggregateDevice> device_;
    std::shared_ptr<aspl::Stream> pushIn_;
    std::shared_ptr<aspl::Stream> pushOut_;
    std::shared_ptr<aspl::Stream> flx4In_;
    std::shared_ptr<aspl::Stream> flx4Out_;

    // Physical devices
    HardwareDevice pushHW_;
    HardwareDevice flx4HW_;

    // Ring buffers: 4 total (push in/out, flx4 in/out)
    TPCircularBuffer pushInputRing_;
    TPCircularBuffer pushOutputRing_;
    TPCircularBuffer flx4InputRing_;
    TPCircularBuffer flx4OutputRing_;

    // Drift tracking
    DriftTracker pushDLL_{44100.0};
    DriftTracker flx4DLL_{44100.0};

    // Adaptive resampler for FLX4 path (stereo)
    SRC_STATE* resamplerIn_  = nullptr;
    SRC_STATE* resamplerOut_ = nullptr;

    // Intermediate buffers for resampling
    float resampleInBuf_[kResampleBufFrames * 2]  = {};
    float resampleOutBuf_[kResampleBufFrames * 2] = {};

    bool ioRunning_ = false;
};
