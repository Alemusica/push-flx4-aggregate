#pragma once

// MachServer: allocates the shared memory region, registers a Mach bootstrap
// service, and hands the memory port to the plugin when it connects.
//
// Protocol:
// 1. Helper starts → allocates SharedMemoryLayout via mach_vm_allocate
// 2. Helper creates a memory entry port (mach_make_memory_entry_64)
// 3. Helper checks in with bootstrap (bootstrap_check_in) under kMachServiceName
// 4. Plugin starts → looks up kMachServiceName (bootstrap_look_up)
// 5. Plugin sends kMsgRequestMemory on that port
// 6. Helper replies with the memory entry port
// 7. Plugin maps the memory with mach_vm_map

#include "SharedMemory.h"

#include <mach/mach.h>
#include <atomic>

namespace flux {

class MachServer {
public:
    MachServer() = default;
    ~MachServer();

    // Allocate shared memory and register the Mach service.
    bool start();

    // Tear down: deregister and deallocate.
    void stop();

    // Run the message receive loop (blocking). Call from a dedicated thread
    // or the main run loop. Handles incoming requests from the plugin.
    void runMessageLoop();

    // Access the shared memory (valid after start()).
    SharedMemoryLayout* sharedMemory() { return sharedMem_; }

    void requestStop() { stopRequested_.store(true, std::memory_order_relaxed); }

private:
    bool allocateSharedMemory();
    bool registerService();
    void handleMessage(mach_msg_header_t* msg);

    SharedMemoryLayout* sharedMem_ = nullptr;
    mach_vm_address_t   sharedMemAddr_ = 0;
    mach_vm_size_t      sharedMemSize_ = 0;
    mach_port_t         memoryEntryPort_ = MACH_PORT_NULL;
    mach_port_t         servicePort_ = MACH_PORT_NULL;

    std::atomic<bool>   stopRequested_{false};
};

} // namespace flux
