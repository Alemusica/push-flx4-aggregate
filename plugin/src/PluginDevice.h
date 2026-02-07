#pragma once

// PluginDevice: aspl::Device subclass that overrides GetZeroTimeStamp to
// derive the virtual device's clock from the Push hardware clock, read
// from shared memory published by the helper daemon.
//
// The plugin NEVER touches CoreAudio client API. All hardware interaction
// is in the helper process. This device just exposes timestamps.

#include "SharedMemory.h"

#include <aspl/Device.hpp>

namespace flux {

class PluginDevice : public aspl::Device {
public:
    PluginDevice(std::shared_ptr<aspl::Context> context,
                 const aspl::DeviceParameters& params,
                 SharedMemoryLayout* shm)
        : aspl::Device(std::move(context), params)
        , shm_(shm)
    {
    }

    void setSharedMemory(SharedMemoryLayout* shm) { shm_ = shm; }

protected:
    // Called by the HAL on the IO thread to get the current clock position.
    // We just read whatever the helper last wrote from Push's IOProc.
    OSStatus GetZeroTimeStamp(Float64* outSampleTime,
                              UInt64*  outHostTime,
                              UInt64*  outSeed) override
    {
        if (shm_) {
            *outSampleTime = shm_->pushClock.sampleTime.load(
                std::memory_order_relaxed);
            *outHostTime = shm_->pushClock.hostTime.load(
                std::memory_order_relaxed);
            *outSeed = shm_->pushClock.seed.load(
                std::memory_order_relaxed);
        } else {
            *outSampleTime = 0.0;
            *outHostTime = 0;
            *outSeed = 0;
        }
        return kAudioHardwareNoError;
    }

private:
    SharedMemoryLayout* shm_ = nullptr;
};

} // namespace flux
