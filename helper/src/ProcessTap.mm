#import "ProcessTap.h"
#import <CoreAudio/AudioHardwareTapping.h>
#import <CoreAudio/CATapDescription.h>
#import <Foundation/Foundation.h>
#import <os/log.h>

namespace flux {

static os_log_t sLog = os_log_create("com.pushflx4.aggregate.helper", "ProcessTap");

// Find a process AudioObjectID by name (e.g., "djay Pro AI").
// CoreAudio assigns AudioObjectIDs to running audio processes.
static AudioObjectID findProcessByName(const std::string& name)
{
    AudioObjectPropertyAddress addr = {
        kAudioHardwarePropertyProcessObjectList,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };

    UInt32 dataSize = 0;
    OSStatus err = AudioObjectGetPropertyDataSize(
        kAudioObjectSystemObject, &addr, 0, nullptr, &dataSize);
    if (err != noErr || dataSize == 0) return kAudioObjectUnknown;

    UInt32 count = dataSize / sizeof(AudioObjectID);
    std::vector<AudioObjectID> processes(count);
    err = AudioObjectGetPropertyData(
        kAudioObjectSystemObject, &addr, 0, nullptr, &dataSize, processes.data());
    if (err != noErr) return kAudioObjectUnknown;

    // Check each process's bundle ID or name.
    for (AudioObjectID proc : processes) {
        AudioObjectPropertyAddress nameAddr = {
            kAudioProcessPropertyBundleID,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain
        };

        CFStringRef bundleID = nullptr;
        UInt32 nameSize = sizeof(bundleID);
        err = AudioObjectGetPropertyData(proc, &nameAddr, 0, nullptr, &nameSize, &bundleID);
        if (err == noErr && bundleID) {
            char buf[256];
            if (CFStringGetCString(bundleID, buf, sizeof(buf), kCFStringEncodingUTF8)) {
                if (std::string(buf).find(name) != std::string::npos) {
                    os_log_info(sLog, "Found process '%{public}s' → ObjectID %u",
                                buf, (unsigned)proc);
                    CFRelease(bundleID);
                    return proc;
                }
            }
            CFRelease(bundleID);
        }
    }

    os_log_error(sLog, "Process '%{public}s' not found in audio process list", name.c_str());
    return kAudioObjectUnknown;
}

ProcessTap::~ProcessTap()
{
    stop();
}

bool ProcessTap::create(const std::string& deviceUID,
                        int streamIndex,
                        const std::string& processName)
{
    @autoreleasepool {
        NSString* uid = [NSString stringWithUTF8String:deviceUID.c_str()];

        CATapDescription* desc = nil;

        if (!processName.empty()) {
            AudioObjectID procID = findProcessByName(processName);
            if (procID == kAudioObjectUnknown) {
                os_log_error(sLog, "Cannot create tap — process not found");
                return false;
            }
            NSArray<NSNumber*>* procs = @[@(procID)];
            desc = [[CATapDescription alloc]
                initWithProcesses:procs
                     andDeviceUID:uid
                       withStream:streamIndex];
        } else {
            // Tap all processes on this device stream.
            desc = [[CATapDescription alloc]
                initExcludingProcesses:@[]
                          andDeviceUID:uid
                            withStream:streamIndex];
        }

        if (!desc) {
            os_log_error(sLog, "Failed to create CATapDescription");
            return false;
        }

        desc.name = @"PushFLX4 Cue Tap";
        desc.muteBehavior = CATapUnmuted;  // Audio still plays on FLX4 headphones
        desc.privateTap = YES;

        OSStatus err = AudioHardwareCreateProcessTap(desc, &tapID_);
        if (err != noErr) {
            os_log_error(sLog, "AudioHardwareCreateProcessTap failed: %d", (int)err);
            return false;
        }

        os_log_info(sLog, "Process tap created: ID %u, device %{public}s stream %d",
                    (unsigned)tapID_, deviceUID.c_str(), streamIndex);

        // The tap object IS an AudioDevice — open IOProc directly on it.
        tapDeviceID_ = tapID_;
        os_log_info(sLog, "Tap device ID: %u", (unsigned)tapDeviceID_);

        return true;
    }
}

bool ProcessTap::start(TapCallback callback)
{
    if (tapDeviceID_ == kAudioObjectUnknown) return false;
    if (running_) return true;

    callback_ = std::move(callback);

    OSStatus err = AudioDeviceCreateIOProcID(
        tapDeviceID_, staticIOProc, this, &ioProcID_);
    if (err != noErr) {
        os_log_error(sLog, "CreateIOProcID on tap device failed: %d", (int)err);
        return false;
    }

    err = AudioDeviceStart(tapDeviceID_, ioProcID_);
    if (err != noErr) {
        os_log_error(sLog, "AudioDeviceStart on tap failed: %d", (int)err);
        AudioDeviceDestroyIOProcID(tapDeviceID_, ioProcID_);
        ioProcID_ = nullptr;
        return false;
    }

    running_ = true;
    os_log_info(sLog, "Tap IOProc started");
    return true;
}

void ProcessTap::stop()
{
    if (running_ && ioProcID_) {
        AudioDeviceStop(tapDeviceID_, ioProcID_);
        AudioDeviceDestroyIOProcID(tapDeviceID_, ioProcID_);
        ioProcID_ = nullptr;
        running_ = false;
    }

    if (tapID_ != kAudioObjectUnknown) {
        AudioHardwareDestroyProcessTap(tapID_);
        tapID_ = kAudioObjectUnknown;
        tapDeviceID_ = kAudioObjectUnknown;
    }
}

OSStatus ProcessTap::staticIOProc(
    AudioObjectID           /*inDevice*/,
    const AudioTimeStamp*   /*inNow*/,
    const AudioBufferList*  inInputData,
    const AudioTimeStamp*   inInputTime,
    AudioBufferList*        /*outOutputData*/,
    const AudioTimeStamp*   /*outOutputTime*/,
    void*                   inClientData)
{
    auto* self = static_cast<ProcessTap*>(inClientData);
    if (self->callback_ && inInputData && inInputData->mNumberBuffers > 0) {
        UInt32 frames = inInputData->mBuffers[0].mDataByteSize
                      / (sizeof(float) * inInputData->mBuffers[0].mNumberChannels);
        self->callback_(inInputData, inInputTime, frames);
    }
    return noErr;
}

} // namespace flux
