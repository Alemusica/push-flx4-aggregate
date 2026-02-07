#pragma once

// AggregateDevice subclasses aspl::Device to override GetZeroTimeStamp,
// locking the virtual device's clock to the Push's USB hardware clock.

#include <aspl/Device.hpp>
#include <atomic>

struct ClockTimestamp {
    Float64 sampleTime = 0.0;
    UInt64  hostTime = 0;
};

class AggregateDevice : public aspl::Device {
public:
    using aspl::Device::Device;

    // Called by the Push IOProc callback to publish the latest timestamp.
    // Lock-free: uses atomic store (relaxed is fine — HAL tolerates jitter).
    void updateClockTimestamp(Float64 sampleTime, UInt64 hostTime)
    {
        // Store as two atomics. The slight race between the two stores
        // is acceptable — GetZeroTimeStamp only needs approximate coherence.
        sampleTime_.store(sampleTime, std::memory_order_relaxed);
        hostTime_.store(hostTime, std::memory_order_relaxed);
    }

    void bumpClockSeed()
    {
        clockSeed_.fetch_add(1, std::memory_order_relaxed);
    }

protected:
    OSStatus GetZeroTimeStamp(Float64* outSampleTime,
                              UInt64*  outHostTime,
                              UInt64*  outSeed) override
    {
        *outSampleTime = sampleTime_.load(std::memory_order_relaxed);
        *outHostTime   = hostTime_.load(std::memory_order_relaxed);
        *outSeed       = clockSeed_.load(std::memory_order_relaxed);
        return kAudioHardwareNoError;
    }

private:
    std::atomic<Float64> sampleTime_{0.0};
    std::atomic<UInt64>  hostTime_{0};
    std::atomic<UInt64>  clockSeed_{0};
};
