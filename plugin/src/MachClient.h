#pragma once

// MachClient: plugin-side IPC. Connects to the helper daemon's Mach bootstrap
// service and maps the shared memory region into this process (coreaudiod).
//
// Called once during plugin initialization. After mapping, the plugin accesses
// SharedMemoryLayout directly — no further Mach messages needed for audio IO.

#include "SharedMemory.h"

#include <mach/mach.h>

namespace flux {

class MachClient {
public:
    MachClient() = default;
    ~MachClient();

    // Look up the helper's Mach service, request the shared memory port,
    // and map it into this process. Returns false if the helper is not running.
    bool connect();

    // Unmap shared memory.
    void disconnect();

    bool isConnected() const { return sharedMem_ != nullptr; }
    SharedMemoryLayout* sharedMemory() { return sharedMem_; }

private:
    SharedMemoryLayout* sharedMem_ = nullptr;
    mach_vm_address_t   mappedAddr_ = 0;
    mach_vm_size_t      mappedSize_ = 0;
};

} // namespace flux
