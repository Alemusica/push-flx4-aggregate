#include "PluginHandler.h"
#include "Constants.h"

#include <os/log.h>
#include <cstring>

namespace flux {

static os_log_t sLog = os_log_create("com.pushflx4.aggregate.plugin", "Handler");

PluginHandler::PluginHandler(
    std::shared_ptr<MachClient> client,
    std::shared_ptr<aspl::Stream> pushIn,
    std::shared_ptr<aspl::Stream> pushOut,
    std::shared_ptr<aspl::Stream> flx4In,
    std::shared_ptr<aspl::Stream> flx4Out,
    std::shared_ptr<aspl::Stream> flx4CueIn)
    : client_(std::move(client))
    , pushIn_(std::move(pushIn))
    , pushOut_(std::move(pushOut))
    , flx4In_(std::move(flx4In))
    , flx4Out_(std::move(flx4Out))
    , flx4CueIn_(std::move(flx4CueIn))
{
}

PluginHandler::~PluginHandler() = default;

OSStatus PluginHandler::OnStartIO()
{
    if (!client_->isConnected()) {
        os_log_info(sLog, "OnStartIO: connecting to helper daemon");
        if (!client_->connect()) {
            os_log_error(sLog, "OnStartIO: helper not available");
            return kAudioHardwareNotRunningError;
        }
    }

    auto* shm = client_->sharedMemory();
    if (!shm || shm->helperStatus.load(std::memory_order_acquire) != kHelperRunning) {
        os_log_error(sLog, "OnStartIO: helper not running");
        return kAudioHardwareNotRunningError;
    }

    os_log_info(sLog, "OnStartIO: connected, helper running");
    return kAudioHardwareNoError;
}

void PluginHandler::OnStopIO()
{
    os_log_info(sLog, "OnStopIO");
}

// ---- Realtime IO ----
// These run on the HAL IO thread. No allocations, no locks, no syscalls.
// Just memcpy between shared memory ring buffers and Ableton's buffers.

void PluginHandler::OnReadClientInput(
    const std::shared_ptr<aspl::Client>& /*client*/,
    const std::shared_ptr<aspl::Stream>& stream,
    Float64 /*zeroTimestamp*/,
    Float64 /*timestamp*/,
    void*   buff,
    UInt32  buffBytesSize)
{
    auto* shm = client_->sharedMemory();
    if (!shm) {
        std::memset(buff, 0, buffBytesSize);
        return;
    }

    if (stream == pushIn_) {
        if (!shm->pushInput.read(buff, buffBytesSize)) {
            std::memset(buff, 0, buffBytesSize);
        }
    }
    else if (stream == flx4In_) {
        // Already resampled to Push clock by the helper.
        if (!shm->flx4Input.read(buff, buffBytesSize)) {
            std::memset(buff, 0, buffBytesSize);
        }
    }
    else if (stream == flx4CueIn_) {
        // Cue audio tapped from djay's FLX4 output, resampled by helper.
        if (!shm->flx4CueInput.read(buff, buffBytesSize)) {
            std::memset(buff, 0, buffBytesSize);
        }
    }
    else {
        std::memset(buff, 0, buffBytesSize);
    }
}

void PluginHandler::OnWriteMixedOutput(
    const std::shared_ptr<aspl::Stream>& stream,
    Float64 /*zeroTimestamp*/,
    Float64 /*timestamp*/,
    const void* buff,
    UInt32 buffBytesSize)
{
    auto* shm = client_->sharedMemory();
    if (!shm) return;

    if (stream == pushOut_) {
        shm->pushOutput.write(buff, buffBytesSize);
    }
    else if (stream == flx4Out_) {
        // Helper will resample from Push clock to FLX4 clock.
        shm->flx4Output.write(buff, buffBytesSize);
    }
}

} // namespace flux
