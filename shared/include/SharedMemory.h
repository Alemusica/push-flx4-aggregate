#pragma once

// Shared memory layout between HAL plugin and helper daemon.
//
// The helper allocates this region via mach_vm_allocate, creates a memory
// entry port (mach_make_memory_entry_64), and hands it to the plugin via
// Mach message. Both processes map the same physical pages.
//
// Lock-free SPSC ring buffers: helper writes audio, plugin reads (input path).
// Plugin writes audio, helper reads (output path).
// Clock timestamps: helper writes, plugin reads (for GetZeroTimeStamp).
//
// All shared fields use atomics or are naturally aligned for lock-free access.

#include "Constants.h"

#include <atomic>
#include <cstring>
#include <cstdint>

namespace flux {

// ---- Lock-free SPSC ring buffer for shared memory ----
// No mmap mirror trick (can't do that across processes). Instead, uses
// modular arithmetic on atomic head/tail indices. The data region is
// inline in the struct so the whole thing lives in a single allocation.

struct alignas(64) SPSCRingBuffer {
    alignas(64) std::atomic<int32_t> head{0};  // Write position (producer)
    alignas(64) std::atomic<int32_t> tail{0};  // Read position (consumer)
    int32_t capacity = 0;
    uint8_t data[kRingBufferCapacity];

    void init(int32_t cap)
    {
        capacity = cap;
        head.store(0, std::memory_order_relaxed);
        tail.store(0, std::memory_order_relaxed);
        std::memset(data, 0, sizeof(data));
    }

    // Available bytes to read.
    int32_t availableRead() const
    {
        int32_t h = head.load(std::memory_order_acquire);
        int32_t t = tail.load(std::memory_order_relaxed);
        int32_t avail = h - t;
        if (avail < 0) avail += capacity;
        return avail;
    }

    // Available space to write.
    int32_t availableWrite() const
    {
        return capacity - 1 - availableRead();
    }

    // Write bytes into the ring buffer. Returns false if not enough space.
    bool write(const void* src, int32_t len)
    {
        if (len > availableWrite()) return false;

        int32_t h = head.load(std::memory_order_relaxed);
        const auto* srcBytes = static_cast<const uint8_t*>(src);

        // May need two memcpy's if wrapping around.
        int32_t firstChunk = capacity - h;
        if (firstChunk >= len) {
            std::memcpy(data + h, srcBytes, len);
        } else {
            std::memcpy(data + h, srcBytes, firstChunk);
            std::memcpy(data, srcBytes + firstChunk, len - firstChunk);
        }

        int32_t newHead = (h + len) % capacity;
        head.store(newHead, std::memory_order_release);
        return true;
    }

    // Read bytes from the ring buffer. Returns false if not enough data.
    bool read(void* dst, int32_t len)
    {
        if (len > availableRead()) return false;

        int32_t t = tail.load(std::memory_order_relaxed);
        auto* dstBytes = static_cast<uint8_t*>(dst);

        int32_t firstChunk = capacity - t;
        if (firstChunk >= len) {
            std::memcpy(dstBytes, data + t, len);
        } else {
            std::memcpy(dstBytes, data + t, firstChunk);
            std::memcpy(dstBytes + firstChunk, data, len - firstChunk);
        }

        int32_t newTail = (t + len) % capacity;
        tail.store(newTail, std::memory_order_release);
        return true;
    }

    // Peek without consuming. Returns pointer to contiguous data up to
    // min(available, capacity - tail). Caller must handle wrap manually
    // or just use read() for simplicity.
    const uint8_t* peek(int32_t* outAvailable) const
    {
        int32_t t = tail.load(std::memory_order_relaxed);
        int32_t avail = availableRead();
        int32_t contiguous = capacity - t;
        *outAvailable = (avail < contiguous) ? avail : contiguous;
        return data + t;
    }

    void clear()
    {
        tail.store(head.load(std::memory_order_relaxed),
                   std::memory_order_release);
    }
};

// ---- Clock data published by the helper (Push master clock) ----

struct alignas(64) ClockData {
    std::atomic<double>   sampleTime{0.0};
    std::atomic<uint64_t> hostTime{0};
    std::atomic<uint64_t> seed{0};
};

// ---- Top-level shared memory layout ----
// Helper writes status + clock + input rings.
// Plugin reads status + clock + input rings, writes output rings.

struct SharedMemoryLayout {
    // Header — status and device state
    std::atomic<uint32_t> helperStatus{kHelperOffline};
    std::atomic<uint32_t> pushState{kDeviceDisconnected};
    std::atomic<uint32_t> flx4State{kDeviceDisconnected};
    uint32_t _pad0 = 0;

    // Push master clock — plugin reads for GetZeroTimeStamp
    ClockData pushClock;

    // Drift ratio (push_rate / flx4_rate) — informational, for monitoring
    std::atomic<double> driftRatio{1.0};

    // Audio ring buffers
    // Input: helper writes (from hardware) → plugin reads (serves to Ableton)
    SPSCRingBuffer pushInput;
    SPSCRingBuffer flx4Input;   // Already resampled to Push clock by helper

    // Output: plugin writes (from Ableton) → helper reads (sends to hardware)
    SPSCRingBuffer pushOutput;
    SPSCRingBuffer flx4Output;  // Helper resamples to FLX4 clock before sending

    void init()
    {
        helperStatus.store(kHelperOffline, std::memory_order_relaxed);
        pushState.store(kDeviceDisconnected, std::memory_order_relaxed);
        flx4State.store(kDeviceDisconnected, std::memory_order_relaxed);
        pushClock.sampleTime.store(0.0, std::memory_order_relaxed);
        pushClock.hostTime.store(0, std::memory_order_relaxed);
        pushClock.seed.store(0, std::memory_order_relaxed);
        driftRatio.store(1.0, std::memory_order_relaxed);
        pushInput.init(kRingBufferCapacity);
        flx4Input.init(kRingBufferCapacity);
        pushOutput.init(kRingBufferCapacity);
        flx4Output.init(kRingBufferCapacity);
    }
};

} // namespace flux
