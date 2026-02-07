#include <aspl/Driver.hpp>
#include <aspl/Plugin.hpp>

#include "Constants.h"
#include "MachClient.h"
#include "PluginDevice.h"
#include "PluginHandler.h"

using namespace flux;

static std::shared_ptr<aspl::Driver> CreateDriver()
{
    auto context = std::make_shared<aspl::Context>();

    // The MachClient will connect to the helper on first OnStartIO.
    auto machClient = std::make_shared<MachClient>();

    // Virtual aggregate device parameters.
    aspl::DeviceParameters params;
    params.Name = "Push+FLX4 Aggregate";
    params.Manufacturer = "PushFLX4";
    params.DeviceUID = "PushFLX4Aggregate_UID";
    params.ModelUID = "PushFLX4Aggregate_ModelUID";
    params.SampleRate = kNominalSampleRate;
    params.ChannelCount = 2;
    params.EnableMixing = true;    // Multi-client (Ableton + system)
    params.Latency = 0;
    params.SafetyOffset = 4;
    params.ClockIsStable = true;
    params.ClockDomain = 0;
    params.CanBeDefault = true;
    params.CanBeDefaultForSystemSounds = false;

    // Device reads clock from shared memory (nullptr until connected).
    auto device = std::make_shared<PluginDevice>(context, params, nullptr);

    // --- Push streams (master, zero added latency) ---
    aspl::StreamParameters pushInParams;
    pushInParams.Direction = aspl::Direction::Input;
    pushInParams.Format.mChannelsPerFrame = kChannelsPerDevice;
    pushInParams.Format.mSampleRate = kNominalSampleRate;
    pushInParams.Latency = 0;
    auto pushIn = device->AddStreamAsync(pushInParams);

    aspl::StreamParameters pushOutParams;
    pushOutParams.Direction = aspl::Direction::Output;
    pushOutParams.Format.mChannelsPerFrame = kChannelsPerDevice;
    pushOutParams.Format.mSampleRate = kNominalSampleRate;
    pushOutParams.Latency = 0;
    auto pushOut = device->AddStreamAsync(pushOutParams);

    // --- FLX4 streams (slave, latency = ring buffer + resampler) ---
    aspl::StreamParameters flx4InParams;
    flx4InParams.Direction = aspl::Direction::Input;
    flx4InParams.Format.mChannelsPerFrame = kChannelsPerDevice;
    flx4InParams.Format.mSampleRate = kNominalSampleRate;
    flx4InParams.Latency = kFLX4StreamLatency;
    auto flx4In = device->AddStreamAsync(flx4InParams);

    aspl::StreamParameters flx4OutParams;
    flx4OutParams.Direction = aspl::Direction::Output;
    flx4OutParams.Format.mChannelsPerFrame = kChannelsPerDevice;
    flx4OutParams.Format.mSampleRate = kNominalSampleRate;
    flx4OutParams.Latency = kFLX4StreamLatency;
    auto flx4Out = device->AddStreamAsync(flx4OutParams);

    // Wire handler — connects shared memory to streams.
    auto handler = std::make_shared<PluginHandler>(
        machClient, pushIn, pushOut, flx4In, flx4Out);
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
