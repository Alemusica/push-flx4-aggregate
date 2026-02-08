#pragma once

// Shared constants between HAL plugin and helper daemon.
// Both processes include this header — keep it dependency-free.

#include <cstdint>

namespace flux {

// Mach bootstrap service name. The plugin looks this up to find the helper.
// Must match AudioServerPlugIn_MachServices in Info.plist.
constexpr const char* kMachServiceName = "com.pushflx4.aggregate.helper";

// Ring buffer capacity per stream (bytes).
// 65536 bytes = ~370ms at 44100Hz stereo float32. Enough runway for DLL
// convergence (~2-5s) without underruns, while keeping latency low.
constexpr int32_t kRingBufferCapacity = 65536;

// Number of channels per device (stereo).
constexpr uint32_t kChannelsPerDevice = 2;

// Bytes per frame (stereo float32).
constexpr uint32_t kBytesPerFrame = kChannelsPerDevice * sizeof(float);

// Default nominal sample rate (Push 3 runs at 48kHz).
constexpr double kNominalSampleRate = 48000.0;

// Default device UIDs.
constexpr const char* kDefaultPushUID =
    "AppleUSBAudioEngine:Ableton:Ableton Push 3:37589272:2,3";
constexpr const char* kDefaultFLX4UID =
    "AppleUSBAudioEngine:AlphaTheta Corporation:DDJ-FLX4:DKVC227610NN:2,1";

// FLX4 slave path latency reported to Ableton for delay compensation.
// Ring buffer target fill (~1024 frames) + resampler group delay (~64 frames).
constexpr uint32_t kFLX4StreamLatency = 1088;

// Process tap: djay Pro AI bundle ID substring for findProcessByName().
constexpr const char* kDjayBundleSubstring = "algoriddim";

// FLX4 output stream index for cue (0-based). Stream 0 = outputs 1-2 (master),
// stream 1 = outputs 3-4 (cue/headphones).
constexpr int kFLX4CueStreamIndex = 1;

// Mach message IDs for the IPC protocol.
enum MachMsgID : uint32_t {
    kMsgRequestMemory = 100,    // Plugin → Helper: "give me the shared memory"
    kMsgMemoryReply   = 101,    // Helper → Plugin: reply with memory port
};

// Helper status flags (in shared memory header).
enum HelperStatus : uint32_t {
    kHelperOffline      = 0,
    kHelperRunning      = 1,
    kHelperError        = 2,
};

// Device connection state (in shared memory header).
enum DeviceState : uint32_t {
    kDeviceDisconnected = 0,
    kDeviceConnected    = 1,
    kDeviceRunning      = 2,
};

} // namespace flux
