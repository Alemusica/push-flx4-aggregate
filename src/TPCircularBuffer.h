// TPCircularBuffer by Michael Tyson
// https://github.com/michaeltyson/TPCircularBuffer
//
// Virtual-memory-backed lock-free ring buffer. Maps the same physical pages
// twice contiguously so reads never need wrap-around logic.
//
// License: MIT-style, see TPCircularBuffer.c

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void    *buffer;
    int32_t  length;
    int32_t  tail;
    int32_t  head;
    volatile int32_t fillCount;
} TPCircularBuffer;

// Initialize with capacity in bytes. Returns false on failure.
bool TPCircularBufferInit(TPCircularBuffer *buffer, int32_t length);

// Clean up virtual memory mappings.
void TPCircularBufferCleanup(TPCircularBuffer *buffer);

// Reset read/write positions to empty.
void TPCircularBufferClear(TPCircularBuffer *buffer);

// --- Producer API (write side) ---

// Get pointer to writable region and available space.
static __inline__ __attribute__((always_inline))
void* TPCircularBufferHead(TPCircularBuffer *buffer, int32_t *availableBytes)
{
    *availableBytes = buffer->length - buffer->fillCount;
    if (*availableBytes == 0) return NULL;
    return (void*)((char*)buffer->buffer + buffer->head);
}

// Mark bytes as written.
static __inline__ __attribute__((always_inline))
void TPCircularBufferProduce(TPCircularBuffer *buffer, int32_t amount)
{
    buffer->head = (buffer->head + amount) % buffer->length;
    __sync_fetch_and_add(&buffer->fillCount, amount);
}

// Convenience: copy bytes into the buffer. Returns false if insufficient space.
static __inline__ __attribute__((always_inline))
bool TPCircularBufferProduceBytes(TPCircularBuffer *buffer,
                                   const void *src, int32_t len)
{
    int32_t space;
    void *head = TPCircularBufferHead(buffer, &space);
    if (space < len) return false;
    memcpy(head, src, len);
    TPCircularBufferProduce(buffer, len);
    return true;
}

// --- Consumer API (read side) ---

// Get pointer to readable region and available bytes.
static __inline__ __attribute__((always_inline))
void* TPCircularBufferTail(TPCircularBuffer *buffer, int32_t *availableBytes)
{
    *availableBytes = buffer->fillCount;
    if (*availableBytes == 0) return NULL;
    return (void*)((char*)buffer->buffer + buffer->tail);
}

// Mark bytes as consumed.
static __inline__ __attribute__((always_inline))
void TPCircularBufferConsume(TPCircularBuffer *buffer, int32_t amount)
{
    buffer->tail = (buffer->tail + amount) % buffer->length;
    __sync_fetch_and_sub(&buffer->fillCount, amount);
}

#ifdef __cplusplus
}
#endif
