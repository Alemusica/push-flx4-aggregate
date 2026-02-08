#pragma once

// PluginHandler: IORequestHandler + ControlRequestHandler for the virtual device.
//
// Pure passthrough — reads audio from shared memory ring buffers (written by
// the helper daemon) and serves it to Ableton. Writes Ableton's output back
// to shared memory for the helper to send to hardware.
//
// No resampling, no DLL, no hardware access. All that is in the helper.

#include "MachClient.h"
#include "SharedMemory.h"

#include <aspl/ControlRequestHandler.hpp>
#include <aspl/IORequestHandler.hpp>
#include <aspl/Stream.hpp>
#include <memory>

namespace flux {

class PluginHandler : public aspl::ControlRequestHandler,
                      public aspl::IORequestHandler {
public:
    PluginHandler(
        std::shared_ptr<MachClient> client,
        std::shared_ptr<aspl::Stream> pushIn,
        std::shared_ptr<aspl::Stream> pushOut,
        std::shared_ptr<aspl::Stream> flx4In,
        std::shared_ptr<aspl::Stream> flx4Out,
        std::shared_ptr<aspl::Stream> flx4CueIn);

    ~PluginHandler() override;

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
    std::shared_ptr<MachClient>    client_;
    std::shared_ptr<aspl::Stream>  pushIn_;
    std::shared_ptr<aspl::Stream>  pushOut_;
    std::shared_ptr<aspl::Stream>  flx4In_;
    std::shared_ptr<aspl::Stream>  flx4Out_;
    std::shared_ptr<aspl::Stream>  flx4CueIn_;
};

} // namespace flux
