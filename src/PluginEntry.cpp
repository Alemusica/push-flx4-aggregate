#include <aspl/Driver.hpp>
#include <aspl/Plugin.hpp>

#include "AggregateDevice.h"
#include "AggregateHandler.h"

static std::shared_ptr<aspl::Driver> CreateDriver()
{
    auto context = std::make_shared<aspl::Context>();

    // Virtual aggregate device parameters
    aspl::DeviceParameters params;
    params.Name = "Push+FLX4 Aggregate";
    params.Manufacturer = "Custom";
    params.DeviceUID = "PushFLX4Aggregate_UID";
    params.ModelUID = "PushFLX4Aggregate_ModelUID";
    params.SampleRate = 44100;
    params.ChannelCount = 2;       // Per-stream channel count
    params.EnableMixing = true;    // Multi-client support
    params.Latency = 0;
    params.SafetyOffset = 4;
    params.ClockIsStable = true;
    params.ClockDomain = 0;        // Own clock domain
    params.CanBeDefault = true;
    params.CanBeDefaultForSystemSounds = false;

    auto device = std::make_shared<AggregateDevice>(context, params);

    // --- Push streams (master clock, zero added latency) ---
    aspl::StreamParameters pushInParams;
    pushInParams.Direction = aspl::Direction::Input;
    pushInParams.Format.mChannelsPerFrame = 2;
    pushInParams.Format.mSampleRate = 44100;
    pushInParams.Latency = 0;
    auto pushIn = device->AddStreamAsync(pushInParams);

    aspl::StreamParameters pushOutParams;
    pushOutParams.Direction = aspl::Direction::Output;
    pushOutParams.Format.mChannelsPerFrame = 2;
    pushOutParams.Format.mSampleRate = 44100;
    pushOutParams.Latency = 0;
    auto pushOut = device->AddStreamAsync(pushOutParams);

    // --- FLX4 streams (slave, latency = ring buffer + resampler delay) ---
    aspl::StreamParameters flx4InParams;
    flx4InParams.Direction = aspl::Direction::Input;
    flx4InParams.Format.mChannelsPerFrame = 2;
    flx4InParams.Format.mSampleRate = 44100;
    flx4InParams.Latency = 1088;   // ~24.7ms ring buffer target + resampler group delay
    auto flx4In = device->AddStreamAsync(flx4InParams);

    aspl::StreamParameters flx4OutParams;
    flx4OutParams.Direction = aspl::Direction::Output;
    flx4OutParams.Format.mChannelsPerFrame = 2;
    flx4OutParams.Format.mSampleRate = 44100;
    flx4OutParams.Latency = 1088;
    auto flx4Out = device->AddStreamAsync(flx4OutParams);

    // Wire IO handler
    auto handler = std::make_shared<AggregateHandler>(
        device, pushIn, pushOut, flx4In, flx4Out);
    device->SetControlHandler(handler);
    device->SetIOHandler(handler);

    auto plugin = std::make_shared<aspl::Plugin>(context);
    plugin->AddDevice(device);

    return std::make_shared<aspl::Driver>(context, plugin);
}

extern "C" void* PushFLX4PluginFactory(
    CFAllocatorRef /*allocator*/,
    CFUUIDRef typeUUID)
{
    if (!CFEqual(typeUUID, kAudioServerPlugInTypeUUID)) {
        return nullptr;
    }
    static auto driver = CreateDriver();
    return driver->GetReference();
}
