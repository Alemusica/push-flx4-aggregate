#include "HardwareDevice.h"
#include <os/log.h>

static os_log_t sLog = os_log_create("com.custom.audio.PushFLX4Aggregate", "HardwareDevice");

HardwareDevice::~HardwareDevice()
{
    stop();
}

bool HardwareDevice::open(const std::string& deviceUID)
{
    CFStringRef uidRef = CFStringCreateWithCString(
        kCFAllocatorDefault, deviceUID.c_str(), kCFStringEncodingUTF8);
    if (!uidRef) return false;

    AudioObjectPropertyAddress addr = {
        kAudioHardwarePropertyTranslateUIDToDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };

    AudioDeviceID devID = kAudioObjectUnknown;
    UInt32 size = sizeof(devID);
    OSStatus err = AudioObjectGetPropertyData(
        kAudioObjectSystemObject, &addr,
        sizeof(uidRef), &uidRef, &size, &devID);

    CFRelease(uidRef);

    if (err != noErr || devID == kAudioObjectUnknown) {
        os_log_error(sLog, "Failed to find device UID '%{public}s': %d",
                     deviceUID.c_str(), (int)err);
        return false;
    }

    deviceID_ = devID;
    os_log_info(sLog, "Opened device '%{public}s' → ID %u",
                deviceUID.c_str(), (unsigned)devID);
    return true;
}

bool HardwareDevice::start(IOCallback callback)
{
    if (deviceID_ == kAudioObjectUnknown) return false;
    if (running_) return true;

    callback_ = std::move(callback);

    OSStatus err = AudioDeviceCreateIOProcID(
        deviceID_, staticIOProc, this, &ioProcID_);
    if (err != noErr) {
        os_log_error(sLog, "CreateIOProcID failed: %d", (int)err);
        return false;
    }

    err = AudioDeviceStart(deviceID_, ioProcID_);
    if (err != noErr) {
        os_log_error(sLog, "AudioDeviceStart failed: %d", (int)err);
        AudioDeviceDestroyIOProcID(deviceID_, ioProcID_);
        ioProcID_ = nullptr;
        return false;
    }

    running_ = true;
    os_log_info(sLog, "Started IOProc on device %u", (unsigned)deviceID_);
    return true;
}

void HardwareDevice::stop()
{
    if (!running_ || deviceID_ == kAudioObjectUnknown) return;

    if (ioProcID_) {
        AudioDeviceStop(deviceID_, ioProcID_);
        AudioDeviceDestroyIOProcID(deviceID_, ioProcID_);
        ioProcID_ = nullptr;
    }

    running_ = false;
    os_log_info(sLog, "Stopped IOProc on device %u", (unsigned)deviceID_);
}

double HardwareDevice::nominalSampleRate() const
{
    if (deviceID_ == kAudioObjectUnknown) return 0.0;

    AudioObjectPropertyAddress addr = {
        kAudioDevicePropertyNominalSampleRate,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };

    Float64 rate = 0.0;
    UInt32 size = sizeof(rate);
    AudioObjectGetPropertyData(deviceID_, &addr, 0, nullptr, &size, &rate);
    return rate;
}

UInt32 HardwareDevice::deviceLatency(bool input) const
{
    if (deviceID_ == kAudioObjectUnknown) return 0;

    AudioObjectPropertyAddress addr = {
        kAudioDevicePropertyLatency,
        input ? kAudioObjectPropertyScopeInput : kAudioObjectPropertyScopeOutput,
        kAudioObjectPropertyElementMain
    };

    UInt32 latency = 0;
    UInt32 size = sizeof(latency);
    AudioObjectGetPropertyData(deviceID_, &addr, 0, nullptr, &size, &latency);
    return latency;
}

UInt32 HardwareDevice::safetyOffset(bool input) const
{
    if (deviceID_ == kAudioObjectUnknown) return 0;

    AudioObjectPropertyAddress addr = {
        kAudioDevicePropertySafetyOffset,
        input ? kAudioObjectPropertyScopeInput : kAudioObjectPropertyScopeOutput,
        kAudioObjectPropertyElementMain
    };

    UInt32 offset = 0;
    UInt32 size = sizeof(offset);
    AudioObjectGetPropertyData(deviceID_, &addr, 0, nullptr, &size, &offset);
    return offset;
}

UInt32 HardwareDevice::bufferFrameSize() const
{
    if (deviceID_ == kAudioObjectUnknown) return 0;

    AudioObjectPropertyAddress addr = {
        kAudioDevicePropertyBufferFrameSize,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };

    UInt32 frames = 0;
    UInt32 size = sizeof(frames);
    AudioObjectGetPropertyData(deviceID_, &addr, 0, nullptr, &size, &frames);
    return frames;
}

OSStatus HardwareDevice::staticIOProc(
    AudioObjectID           inDevice,
    const AudioTimeStamp*   inNow,
    const AudioBufferList*  inInputData,
    const AudioTimeStamp*   inInputTime,
    AudioBufferList*        outOutputData,
    const AudioTimeStamp*   outOutputTime,
    void*                   inClientData)
{
    auto* self = static_cast<HardwareDevice*>(inClientData);
    if (self->callback_) {
        self->callback_(inDevice, inNow, inInputData, inInputTime,
                        outOutputData, outOutputTime);
    }
    return noErr;
}
