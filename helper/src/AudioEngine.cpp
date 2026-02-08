#include "AudioEngine.h"
#include <os/log.h>
#include <cstring>

namespace flux {

static os_log_t sLog = os_log_create("com.pushflx4.aggregate.helper", "AudioEngine");

AudioEngine::AudioEngine(SharedMemoryLayout* shm,
                         const std::string& pushUID,
                         const std::string& flx4UID)
    : shm_(shm)
    , pushUID_(pushUID)
    , flx4UID_(flx4UID)
{
}

AudioEngine::~AudioEngine()
{
    stop();
}

bool AudioEngine::start()
{
    if (running_) return true;

    // Initialize resamplers (stereo, medium quality — 97dB SNR, 90% bandwidth).
    int err;
    resamplerIn_ = src_new(SRC_SINC_MEDIUM_QUALITY, kChannelsPerDevice, &err);
    if (!resamplerIn_) {
        os_log_error(sLog, "Failed to create input resampler: %s",
                     src_strerror(err));
        return false;
    }
    resamplerOut_ = src_new(SRC_SINC_MEDIUM_QUALITY, kChannelsPerDevice, &err);
    if (!resamplerOut_) {
        os_log_error(sLog, "Failed to create output resampler: %s",
                     src_strerror(err));
        src_delete(resamplerIn_);
        resamplerIn_ = nullptr;
        return false;
    }
    resamplerCue_ = src_new(SRC_SINC_MEDIUM_QUALITY, kChannelsPerDevice, &err);
    if (!resamplerCue_) {
        os_log_error(sLog, "Failed to create cue resampler: %s",
                     src_strerror(err));
        // Non-fatal: cue tap is optional. Continue without it.
    }

    // Open Push (master clock).
    if (pushHW_.open(pushUID_)) {
        double pushRate = pushHW_.nominalSampleRate();
        pushDLL_ = DriftTracker(pushRate > 0 ? pushRate : 48000.0);
        os_log_info(sLog, "Push sample rate: %.0f Hz", pushRate);
        shm_->pushState.store(kDeviceConnected, std::memory_order_release);
        pushHW_.start([this](auto... args) { onPushIO(args...); });
        if (pushHW_.isRunning()) {
            shm_->pushState.store(kDeviceRunning, std::memory_order_release);
        }
    } else {
        pushDLL_.reset();
        os_log_error(sLog, "Push not found — will retry on hot-plug");
    }

    // Open FLX4 (slave).
    if (flx4HW_.open(flx4UID_)) {
        double flx4Rate = flx4HW_.nominalSampleRate();
        flx4DLL_ = DriftTracker(flx4Rate > 0 ? flx4Rate : 48000.0);
        os_log_info(sLog, "FLX4 sample rate: %.0f Hz", flx4Rate);
        shm_->flx4State.store(kDeviceConnected, std::memory_order_release);
        flx4HW_.start([this](auto... args) { onFLX4IO(args...); });
        if (flx4HW_.isRunning()) {
            shm_->flx4State.store(kDeviceRunning, std::memory_order_release);
        }
    } else {
        flx4DLL_.reset();
        os_log_error(sLog, "FLX4 not found — will retry on hot-plug");
    }

    // ---- Cue process tap (djay → FLX4 output stream 1 = cue channels 3-4) ----
    if (flx4HW_.isRunning() && resamplerCue_) {
        if (cueTap_.create(flx4UID_, kFLX4CueStreamIndex, kDjayBundleSubstring)) {
            cueTap_.start([this](const AudioBufferList* inData,
                                 const AudioTimeStamp* /*inTime*/,
                                 UInt32 frameCount) {
                // Tap callback — runs on the tap's IO thread.
                // Resample from FLX4 clock → Push clock, write to cue ring buffer.
                if (!inData || inData->mNumberBuffers == 0 || !resamplerCue_) return;

                const auto& buf = inData->mBuffers[0];
                bool dllReady = pushDLL_.isStable() && flx4DLL_.isStable();

                if (dllReady) {
                    double ratio = pushDLL_.rate() / flx4DLL_.rate();
                    uint32_t maxOutput = static_cast<uint32_t>(
                        static_cast<double>(frameCount) * ratio + 4);
                    if (maxOutput > kResampleBufFrames) maxOutput = kResampleBufFrames;

                    SRC_DATA data;
                    data.data_in = static_cast<const float*>(buf.mData);
                    data.data_out = cueResampleBuf_;
                    data.input_frames = frameCount;
                    data.output_frames = maxOutput;
                    data.src_ratio = ratio;
                    data.end_of_input = 0;

                    if (src_process(resamplerCue_, &data) == 0
                        && data.output_frames_gen > 0) {
                        shm_->flx4CueInput.write(
                            cueResampleBuf_,
                            data.output_frames_gen * kBytesPerFrame);
                    }
                } else {
                    // DLL not stable — pass through raw.
                    shm_->flx4CueInput.write(buf.mData, buf.mDataByteSize);
                }
            });
            os_log_info(sLog, "Cue tap started on FLX4 stream %d", kFLX4CueStreamIndex);
        } else {
            os_log_info(sLog, "Cue tap not available (djay not running?) — will work without cue");
        }
    }

    shm_->helperStatus.store(kHelperRunning, std::memory_order_release);
    running_ = true;
    os_log_info(sLog, "AudioEngine started (Push: %s, FLX4: %s, Cue: %s)",
                pushHW_.isRunning() ? "running" : "offline",
                flx4HW_.isRunning() ? "running" : "offline",
                cueTap_.isRunning() ? "tapped" : "off");
    return true;
}

void AudioEngine::stop()
{
    if (!running_) return;

    cueTap_.stop();
    pushHW_.stop();
    flx4HW_.stop();

    if (resamplerIn_) { src_delete(resamplerIn_); resamplerIn_ = nullptr; }
    if (resamplerOut_) { src_delete(resamplerOut_); resamplerOut_ = nullptr; }
    if (resamplerCue_) { src_delete(resamplerCue_); resamplerCue_ = nullptr; }

    shm_->pushState.store(kDeviceDisconnected, std::memory_order_release);
    shm_->flx4State.store(kDeviceDisconnected, std::memory_order_release);
    shm_->helperStatus.store(kHelperOffline, std::memory_order_release);
    running_ = false;

    os_log_info(sLog, "AudioEngine stopped");
}

// ---- Push IOProc (master clock) ----
// Direct passthrough: hardware → shared memory, shared memory → hardware.
// Also publishes clock timestamps for the plugin's GetZeroTimeStamp.

void AudioEngine::onPushIO(
    AudioDeviceID /*device*/,
    const AudioTimeStamp* now,
    const AudioBufferList* inputData,
    const AudioTimeStamp* inputTime,
    AudioBufferList* outputData,
    const AudioTimeStamp* outputTime)
{
    // Update Push DLL.
    if (now->mFlags & kAudioTimeStampHostTimeValid) {
        uint32_t frames = 0;
        if (inputData && inputData->mNumberBuffers > 0) {
            frames = inputData->mBuffers[0].mDataByteSize / kBytesPerFrame;
        }
        pushDLL_.update(now->mHostTime, frames);
    }

    // Publish Push clock → plugin reads this in GetZeroTimeStamp.
    if (inputTime
        && (inputTime->mFlags & kAudioTimeStampSampleTimeValid)
        && (inputTime->mFlags & kAudioTimeStampHostTimeValid))
    {
        shm_->pushClock.sampleTime.store(
            inputTime->mSampleTime, std::memory_order_relaxed);
        shm_->pushClock.hostTime.store(
            inputTime->mHostTime, std::memory_order_relaxed);
    }

    // Push input → shared memory (for plugin to serve to Ableton).
    if (inputData && inputData->mNumberBuffers > 0) {
        const auto& buf = inputData->mBuffers[0];
        shm_->pushInput.write(buf.mData, buf.mDataByteSize);
    }

    // Shared memory → Push output (Ableton's audio going to Push hardware).
    if (outputData && outputData->mNumberBuffers > 0) {
        auto& buf = outputData->mBuffers[0];
        if (!shm_->pushOutput.read(buf.mData, buf.mDataByteSize)) {
            std::memset(buf.mData, 0, buf.mDataByteSize);
        }
    }
}

// ---- FLX4 IOProc (slave — resampled to/from Push clock) ----
// Input: read from FLX4 hardware, resample to Push clock, write to shared memory.
// Output: read from shared memory, resample to FLX4 clock, write to hardware.

void AudioEngine::onFLX4IO(
    AudioDeviceID /*device*/,
    const AudioTimeStamp* now,
    const AudioBufferList* inputData,
    const AudioTimeStamp* /*inputTime*/,
    AudioBufferList* outputData,
    const AudioTimeStamp* /*outputTime*/)
{
    // Update FLX4 DLL.
    if (now->mFlags & kAudioTimeStampHostTimeValid) {
        uint32_t frames = 0;
        if (inputData && inputData->mNumberBuffers > 0) {
            frames = inputData->mBuffers[0].mDataByteSize / kBytesPerFrame;
        }
        flx4DLL_.update(now->mHostTime, frames);
    }

    // Publish drift ratio for monitoring.
    if (pushDLL_.isStable() && flx4DLL_.isStable()) {
        shm_->driftRatio.store(
            pushDLL_.rate() / flx4DLL_.rate(), std::memory_order_relaxed);
    }

    bool dllReady = pushDLL_.isStable() && flx4DLL_.isStable();

    // ---- FLX4 Input → resample → shared memory ----
    if (inputData && inputData->mNumberBuffers > 0 && resamplerIn_ && dllReady) {
        const auto& buf = inputData->mBuffers[0];
        uint32_t inputFrames = buf.mDataByteSize / kBytesPerFrame;
        double ratio = pushDLL_.rate() / flx4DLL_.rate();

        uint32_t maxOutput = static_cast<uint32_t>(
            static_cast<double>(inputFrames) * ratio + 4);
        if (maxOutput > kResampleBufFrames) maxOutput = kResampleBufFrames;

        SRC_DATA data;
        data.data_in = static_cast<const float*>(buf.mData);
        data.data_out = resampleBuf_;
        data.input_frames = inputFrames;
        data.output_frames = maxOutput;
        data.src_ratio = ratio;
        data.end_of_input = 0;

        if (src_process(resamplerIn_, &data) == 0 && data.output_frames_gen > 0) {
            shm_->flx4Input.write(
                resampleBuf_,
                data.output_frames_gen * kBytesPerFrame);
        }
    } else if (inputData && inputData->mNumberBuffers > 0) {
        // DLL not stable yet — pass through raw (better than silence).
        const auto& buf = inputData->mBuffers[0];
        shm_->flx4Input.write(buf.mData, buf.mDataByteSize);
    }

    // ---- Shared memory → resample → FLX4 Output ----
    if (outputData && outputData->mNumberBuffers > 0) {
        auto& buf = outputData->mBuffers[0];
        uint32_t outputFrames = buf.mDataByteSize / kBytesPerFrame;

        if (resamplerOut_ && dllReady) {
            double ratio = flx4DLL_.rate() / pushDLL_.rate();

            // Read enough Push-clock-domain frames to produce outputFrames
            // in FLX4-clock-domain after resampling.
            uint32_t inputNeeded = static_cast<uint32_t>(
                static_cast<double>(outputFrames) / ratio + 4);
            uint32_t inputBytes = inputNeeded * kBytesPerFrame;

            // Use resampleBuf_ as temporary input storage.
            float tempIn[kResampleBufFrames * kChannelsPerDevice];
            int32_t available = shm_->flx4Output.availableRead();
            if (available >= static_cast<int32_t>(inputBytes)) {
                shm_->flx4Output.read(tempIn, inputBytes);

                SRC_DATA data;
                data.data_in = tempIn;
                data.data_out = static_cast<float*>(buf.mData);
                data.input_frames = inputNeeded;
                data.output_frames = outputFrames;
                data.src_ratio = ratio;
                data.end_of_input = 0;

                if (src_process(resamplerOut_, &data) != 0
                    || data.output_frames_gen < outputFrames)
                {
                    // Partial output — zero-pad the rest.
                    uint32_t filled = data.output_frames_gen * kBytesPerFrame;
                    if (filled < buf.mDataByteSize) {
                        std::memset(
                            static_cast<uint8_t*>(buf.mData) + filled,
                            0, buf.mDataByteSize - filled);
                    }
                }
            } else {
                std::memset(buf.mData, 0, buf.mDataByteSize);
            }
        } else {
            // DLL not ready — try direct passthrough.
            if (!shm_->flx4Output.read(buf.mData, buf.mDataByteSize)) {
                std::memset(buf.mData, 0, buf.mDataByteSize);
            }
        }
    }
}

} // namespace flux
