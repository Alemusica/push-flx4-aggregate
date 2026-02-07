// TPCircularBuffer by Michael Tyson
// https://github.com/michaeltyson/TPCircularBuffer
//
// This implementation uses mach VM to create a mirrored memory region,
// eliminating wrap-around copies entirely.
//
// Original license: permissive / public domain style.

#include "TPCircularBuffer.h"
#include <mach/mach.h>
#include <stdlib.h>
#include <string.h>

#define kTPCircularBufferMinSize 16384

bool TPCircularBufferInit(TPCircularBuffer *buffer, int32_t length)
{
    // Round up to page size
    vm_size_t pageSize = 0;
    host_page_size(mach_host_self(), &pageSize);

    if (length < kTPCircularBufferMinSize) {
        length = kTPCircularBufferMinSize;
    }

    // Round to page boundary
    length = (int32_t)(((length + pageSize - 1) / pageSize) * pageSize);

    buffer->length = length;
    buffer->tail = 0;
    buffer->head = 0;
    buffer->fillCount = 0;

    // Allocate double-size virtual region
    vm_address_t bufferAddress = 0;
    kern_return_t result = vm_allocate(mach_task_self(),
                                        &bufferAddress,
                                        length * 2,
                                        VM_FLAGS_ANYWHERE);
    if (result != KERN_SUCCESS) {
        return false;
    }

    // Deallocate the second half
    result = vm_deallocate(mach_task_self(),
                           bufferAddress + length,
                           length);
    if (result != KERN_SUCCESS) {
        vm_deallocate(mach_task_self(), bufferAddress, length * 2);
        return false;
    }

    // Re-map the first half into the second half's address space
    vm_address_t virtualAddress = bufferAddress + length;
    vm_prot_t curProtection, maxProtection;
    result = vm_remap(mach_task_self(),
                      &virtualAddress,
                      length,
                      0,                   // mask
                      0,                   // anywhere = false
                      mach_task_self(),
                      bufferAddress,
                      0,                   // copy = false (share)
                      &curProtection,
                      &maxProtection,
                      VM_INHERIT_DEFAULT);

    if (result != KERN_SUCCESS || virtualAddress != bufferAddress + length) {
        vm_deallocate(mach_task_self(), bufferAddress, length);
        return false;
    }

    buffer->buffer = (void*)bufferAddress;
    return true;
}

void TPCircularBufferCleanup(TPCircularBuffer *buffer)
{
    if (buffer->buffer) {
        vm_deallocate(mach_task_self(),
                      (vm_address_t)buffer->buffer,
                      buffer->length * 2);
        buffer->buffer = NULL;
    }
}

void TPCircularBufferClear(TPCircularBuffer *buffer)
{
    buffer->tail = 0;
    buffer->head = 0;
    __sync_synchronize();
    buffer->fillCount = 0;
}
