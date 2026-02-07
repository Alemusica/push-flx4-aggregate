#include "AggregateHandler.h"
#include <os/log.h>
#include <cstring>

static os_log_t sLog = os_log_create("com.custom.audio.PushFLX4Aggregate", "Handler");

AggregateHandler::AggregateHandler(
    std::shared_ptr<AggregateDevice> device,
    std::shared_ptr<aspl::Stream> pushIn,
    std::shared_ptr<aspl::Stream> pushOut,
    std::shared_ptr<aspl::Stream> flx4In,
    std::shared_ptr<aspl::Stream> flx4Out)
    : device_(std::move(device))
    , pushIn_(std::move(pushIn))
    , pushOut_(std::move(pushOut))
    , flx4In_(std::move(flx4In))
    , flx4Out_(std::move(flx4Out))
{
}

AggregateHandler::~AggregateHandler()
{
    OnStopIO();
}

OSStatus AggregateHandler::OnStartIO()
{
    if (ioRunning_) return kAudioHardwareNoError;

    os_log_info(sLog, "OnStartIO: opening hardware devices");

    // Initialize ring buffers
    TPCircularBufferInit(&pushInputRing_,  kRingBufferBytes);
    TPCircularBufferInit(&pushOutputRing_, kRingBufferBytes);
    TPCircularBufferInit(&flx4InputRing_,  kRingBufferBytes);
    TPCircularBufferInit(&flx4OutputRing_, kRingBufferBytes);

    // Initialize resamplers (stereo, medium quality)
    int err;
    resamplerIn_ = src_new(SRC_SINC_MEDIUM_QUALITY, 2, &err);
    if (!resamplerIn_) {
        os_log_error(sLog, "Failed to create input resampler: %s",
                     src_strerror(err));
        return kAudioHardwareUnspecifiedError;
    }

    resamplerOut_ = src_new(SRC_SINC_MEDIUM_QUALITY, 2, &err);
    if (!resamplerOut_) {
        os_log_error(sLog, "Failed to create output resampler: %s",
                     src_strerror(err));
        src_delete(resamplerIn_);
        resamplerIn_ = nullptr;
        return kAudioHardwareUnspecifiedError;
    }

    // Reset DLLs
    pushDLL_.reset();
    flx4DLL_.reset();

    // Open Push
    if (!pushHW_.open(kPushUID)) {
        os_log_error(sLog, "Failed to open Push — check UID");
        // Continue anyway; will output silence until connected
    }

    // Open FLX4
    if (!flx4HW_.open(kFLX4UID)) {
        os_log_error(sLog, "Failed to open FLX4 — check UID");
    }

    // Start IOProcs
    if (pushHW_.deviceID() != kAudioObjectUnknown) {
        pushHW_.start([this](auto... args) { pushIOCallback(args...); });
    }
    if (flx4HW_.deviceID() != kAudioObjectUnknown) {
        flx4HW_.start([this](auto... args) { flx4IOCallback(args...); });
    }

    ioRunning_ = true;
    os_log_info(sLog, "OnStartIO: running");
    return kAudioHardwareNoError;
}

void AggregateHandler::OnStopIO()
{
    if (!ioRunning_) return;

    os_log_info(sLog, "OnStopIO: stopping hardware devices");

    pushHW_.stop();
    flx4HW_.stop();

    TPCircularBufferCleanup(&pushInputRing_);
    TPCircularBufferCleanup(&pushOutputRing_);
    TPCircularBufferCleanup(&flx4InputRing_);
    TPCircularBufferCleanup(&flx4OutputRing_);

    if (resamplerIn_) {
        src_delete(resamplerIn_);
        resamplerIn_ = nullptr;
    }
    if (resamplerOut_) {
        src_delete(resamplerOut_);
        resamplerOut_ = nullptr;
    }

    ioRunning_ = false;
}

// --- Hardware IOProc callbacks (realtime thread) ---

void AggregateHandler::pushIOCallback(
    AudioDeviceID /*device*/,
    const AudioTimeStamp* now,
    const AudioBufferList* inputData,
    const AudioTimeStamp* inputTime,
    AudioBufferList* outputData,
    const AudioTimeStamp* outputTime)
{
    // Update drift tracker
    if (now->mFlags & kAudioTimeStampHostTimeValid) {
        UInt32 frames = 0;
        if (inputData && inputData->mNumberBuffers > 0) {
            frames = inputData->mBuffers[0].mDataByteSize
                   / (sizeof(float) * inputData->mBuffers[0].mNumberChannels);
        }
        pushDLL_.update(now->mHostTime, frames);
    }

    // Publish Push clock to virtual device
    if (inputTime && (inputTime->mFlags & kAudioTimeStampSampleTimeValid)
                  && (inputTime->mFlags & kAudioTimeStampHostTimeValid)) {
        device_->updateClockTimestamp(inputTime->mSampleTime,
                                      inputTime->mHostTime);
    }

    // Capture input: Push → ring buffer
    if (inputData && inputData->mNumberBuffers > 0) {
        const auto& buf = inputData->mBuffers[0];
        TPCircularBufferProduceBytes(&pushInputRing_,
                                      buf.mData, buf.mDataByteSize);
    }

    // Playback output: ring buffer → Push
    if (outputData && outputData->mNumberBuffers > 0) {
        auto& buf = outputData->mBuffers[0];
        int32_t available;
        void* tail = TPCircularBufferTail(&pushOutputRing_, &available);
        if (tail && available >= (int32_t)buf.mDataByteSize) {
            memcpy(buf.mData, tail, buf.mDataByteSize);
            TPCircularBufferConsume(&pushOutputRing_, buf.mDataByteSize);
        } else {
            // Underrun — output silence
            memset(buf.mData, 0, buf.mDataByteSize);
        }
    }
}

void AggregateHandler::flx4IOCallback(
    AudioDeviceID /*device*/,
    const AudioTimeStamp* now,
    const AudioBufferList* inputData,
    const AudioTimeStamp* /*inputTime*/,
    AudioBufferList* outputData,
    const AudioTimeStamp* /*outputTime*/)
{
    // Update drift tracker
    if (now->mFlags & kAudioTimeStampHostTimeValid) {
        UInt32 frames = 0;
        if (inputData && inputData->mNumberBuffers > 0) {
            frames = inputData->mBuffers[0].mDataByteSize
                   / (sizeof(float) * inputData->mBuffers[0].mNumberChannels);
        }
        flx4DLL_.update(now->mHostTime, frames);
    }

    // Capture input: FLX4 → ring buffer (raw, resampled on read)
    if (inputData && inputData->mNumberBuffers > 0) {
        const auto& buf = inputData->mBuffers[0];
        TPCircularBufferProduceBytes(&flx4InputRing_,
                                      buf.mData, buf.mDataByteSize);
    }

    // Playback output: ring buffer → FLX4 (raw, resampled on write)
    if (outputData && outputData->mNumberBuffers > 0) {
        auto& buf = outputData->mBuffers[0];
        int32_t available;
        void* tail = TPCircularBufferTail(&flx4OutputRing_, &available);
        if (tail && available >= (int32_t)buf.mDataByteSize) {
            memcpy(buf.mData, tail, buf.mDataByteSize);
            TPCircularBufferConsume(&flx4OutputRing_, buf.mDataByteSize);
        } else {
            memset(buf.mData, 0, buf.mDataByteSize);
        }
    }
}

// --- Virtual device IO (called by libASPL on the HAL IO thread) ---

void AggregateHandler::OnReadClientInput(
    const std::shared_ptr<aspl::Client>& /*client*/,
    const std::shared_ptr<aspl::Stream>& stream,
    Float64 /*zeroTimestamp*/,
    Float64 /*timestamp*/,
    void*   buff,
    UInt32  buffBytesSize)
{
    if (stream == pushIn_) {
        // Push path: direct copy, no resampling (master clock)
        int32_t available;
        void* tail = TPCircularBufferTail(&pushInputRing_, &available);
        if (tail && available >= (int32_t)buffBytesSize) {
            memcpy(buff, tail, buffBytesSize);
            TPCircularBufferConsume(&pushInputRing_, buffBytesSize);
        } else {
            memset(buff, 0, buffBytesSize);
        }
    }
    else if (stream == flx4In_) {
        // FLX4 path: read from ring buffer and resample to Push clock
        if (!resamplerIn_ || !pushDLL_.isStable() || !flx4DLL_.isStable()) {
            memset(buff, 0, buffBytesSize);
            return;
        }

        double ratio = pushDLL_.rate() / flx4DLL_.rate();
        UInt32 neededFrames = buffBytesSize / (sizeof(float) * 2);

        // Read raw FLX4 samples from ring buffer
        UInt32 inputFramesNeeded = static_cast<UInt32>(
            static_cast<double>(neededFrames) / ratio + 2);
        int32_t inputBytes = inputFramesNeeded * sizeof(float) * 2;

        int32_t available;
        void* tail = TPCircularBufferTail(&flx4InputRing_, &available);
        if (!tail || available < inputBytes) {
            memset(buff, 0, buffBytesSize);
            return;
        }

        SRC_DATA data;
        data.data_in = static_cast<const float*>(tail);
        data.data_out = static_cast<float*>(buff);
        data.input_frames = available / (sizeof(float) * 2);
        data.output_frames = neededFrames;
        data.src_ratio = ratio;
        data.end_of_input = 0;

        src_process(resamplerIn_, &data);
        TPCircularBufferConsume(&flx4InputRing_,
            data.input_frames_used * sizeof(float) * 2);
    }
    else {
        memset(buff, 0, buffBytesSize);
    }
}

void AggregateHandler::OnWriteMixedOutput(
    const std::shared_ptr<aspl::Stream>& stream,
    Float64 /*zeroTimestamp*/,
    Float64 /*timestamp*/,
    const void* buff,
    UInt32 buffBytesSize)
{
    if (stream == pushOut_) {
        // Push path: direct write to ring buffer
        TPCircularBufferProduceBytes(&pushOutputRing_, buff, buffBytesSize);
    }
    else if (stream == flx4Out_) {
        // FLX4 path: resample from Push clock to FLX4 clock, then write
        if (!resamplerOut_ || !pushDLL_.isStable() || !flx4DLL_.isStable()) {
            return;
        }

        double ratio = flx4DLL_.rate() / pushDLL_.rate();
        UInt32 inputFrames = buffBytesSize / (sizeof(float) * 2);
        UInt32 outputFrames = static_cast<UInt32>(
            static_cast<double>(inputFrames) * ratio + 2);

        if (outputFrames > kResampleBufFrames) {
            outputFrames = kResampleBufFrames;
        }

        SRC_DATA data;
        data.data_in = static_cast<const float*>(buff);
        data.data_out = resampleOutBuf_;
        data.input_frames = inputFrames;
        data.output_frames = outputFrames;
        data.src_ratio = ratio;
        data.end_of_input = 0;

        src_process(resamplerOut_, &data);
        TPCircularBufferProduceBytes(&flx4OutputRing_,
            resampleOutBuf_,
            data.output_frames_gen * sizeof(float) * 2);
    }
}
