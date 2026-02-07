#include "MachServer.h"
#include "Constants.h"

#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <servers/bootstrap.h>
#include <os/log.h>
#include <cstring>

namespace flux {

static os_log_t sLog = os_log_create("com.pushflx4.aggregate.helper", "MachServer");

// Mach message structures for the handshake protocol.
struct RequestMsg {
    mach_msg_header_t header;
    mach_msg_trailer_t trailer;
};

struct ReplyMsg {
    mach_msg_header_t header;
    mach_msg_body_t body;
    mach_msg_port_descriptor_t memoryPort;
    // Include the size so the plugin knows how much to map.
    mach_vm_size_t memorySize;
};

MachServer::~MachServer()
{
    stop();
}

bool MachServer::start()
{
    if (!allocateSharedMemory()) return false;
    if (!registerService()) return false;

    os_log_info(sLog, "MachServer started, service: %{public}s", kMachServiceName);
    return true;
}

void MachServer::stop()
{
    stopRequested_.store(true, std::memory_order_relaxed);

    if (servicePort_ != MACH_PORT_NULL) {
        mach_port_deallocate(mach_task_self(), servicePort_);
        servicePort_ = MACH_PORT_NULL;
    }

    if (memoryEntryPort_ != MACH_PORT_NULL) {
        mach_port_deallocate(mach_task_self(), memoryEntryPort_);
        memoryEntryPort_ = MACH_PORT_NULL;
    }

    if (sharedMemAddr_ != 0) {
        mach_vm_deallocate(mach_task_self(), sharedMemAddr_, sharedMemSize_);
        sharedMemAddr_ = 0;
        sharedMem_ = nullptr;
    }
}

bool MachServer::allocateSharedMemory()
{
    sharedMemSize_ = sizeof(SharedMemoryLayout);

    // Round up to page size.
    vm_size_t pageSize = 0;
    host_page_size(mach_host_self(), &pageSize);
    sharedMemSize_ = ((sharedMemSize_ + pageSize - 1) / pageSize) * pageSize;

    kern_return_t kr = mach_vm_allocate(
        mach_task_self(), &sharedMemAddr_, sharedMemSize_,
        VM_FLAGS_ANYWHERE);
    if (kr != KERN_SUCCESS) {
        os_log_error(sLog, "mach_vm_allocate failed: %s", mach_error_string(kr));
        return false;
    }

    sharedMem_ = reinterpret_cast<SharedMemoryLayout*>(sharedMemAddr_);
    sharedMem_->init();

    // Create a memory entry port that the plugin can use to map this region.
    memory_object_size_t entrySize = sharedMemSize_;
    kr = mach_make_memory_entry_64(
        mach_task_self(),
        &entrySize,
        sharedMemAddr_,
        VM_PROT_READ | VM_PROT_WRITE,
        &memoryEntryPort_,
        MACH_PORT_NULL);
    if (kr != KERN_SUCCESS) {
        os_log_error(sLog, "mach_make_memory_entry_64 failed: %s",
                     mach_error_string(kr));
        mach_vm_deallocate(mach_task_self(), sharedMemAddr_, sharedMemSize_);
        sharedMemAddr_ = 0;
        return false;
    }

    os_log_info(sLog, "Shared memory allocated: %llu bytes at %p",
                sharedMemSize_, sharedMem_);
    return true;
}

bool MachServer::registerService()
{
    kern_return_t kr = bootstrap_check_in(
        bootstrap_port, kMachServiceName, &servicePort_);
    if (kr != KERN_SUCCESS) {
        os_log_error(sLog, "bootstrap_check_in failed: %s (is another instance running?)",
                     mach_error_string(kr));
        return false;
    }

    os_log_info(sLog, "Registered Mach service: %{public}s", kMachServiceName);
    return true;
}

void MachServer::runMessageLoop()
{
    // Buffer large enough for request + trailer.
    uint8_t msgBuf[sizeof(RequestMsg) + 256];

    while (!stopRequested_.load(std::memory_order_relaxed)) {
        auto* msg = reinterpret_cast<mach_msg_header_t*>(msgBuf);
        std::memset(msgBuf, 0, sizeof(msgBuf));

        msg->msgh_size = sizeof(msgBuf);
        msg->msgh_local_port = servicePort_;

        // Receive with timeout so we can check stopRequested_ periodically.
        kern_return_t kr = mach_msg(
            msg,
            MACH_RCV_MSG | MACH_RCV_TIMEOUT,
            0,                          // send size
            sizeof(msgBuf),             // receive size
            servicePort_,
            500,                        // 500ms timeout
            MACH_PORT_NULL);

        if (kr == MACH_RCV_TIMED_OUT) {
            continue;
        }

        if (kr != MACH_MSG_SUCCESS) {
            os_log_error(sLog, "mach_msg receive failed: %s", mach_error_string(kr));
            continue;
        }

        handleMessage(msg);
    }
}

void MachServer::handleMessage(mach_msg_header_t* msg)
{
    if (msg->msgh_id == kMsgRequestMemory) {
        os_log_info(sLog, "Plugin requested shared memory");

        ReplyMsg reply;
        std::memset(&reply, 0, sizeof(reply));

        reply.header.msgh_bits =
            MACH_MSGH_BITS(MACH_MSG_TYPE_MOVE_SEND_ONCE, 0)
            | MACH_MSGH_BITS_COMPLEX;
        reply.header.msgh_size = sizeof(reply);
        reply.header.msgh_remote_port = msg->msgh_remote_port;
        reply.header.msgh_local_port = MACH_PORT_NULL;
        reply.header.msgh_id = kMsgMemoryReply;

        reply.body.msgh_descriptor_count = 1;

        reply.memoryPort.name = memoryEntryPort_;
        reply.memoryPort.disposition = MACH_MSG_TYPE_COPY_SEND;
        reply.memoryPort.type = MACH_MSG_PORT_DESCRIPTOR;

        reply.memorySize = sharedMemSize_;

        kern_return_t kr = mach_msg(
            &reply.header,
            MACH_SEND_MSG | MACH_SEND_TIMEOUT,
            sizeof(reply),
            0, MACH_PORT_NULL,
            1000,                       // 1s send timeout
            MACH_PORT_NULL);

        if (kr != MACH_MSG_SUCCESS) {
            os_log_error(sLog, "Failed to send memory reply: %s",
                         mach_error_string(kr));
        } else {
            os_log_info(sLog, "Shared memory port sent to plugin");
        }
    } else {
        os_log_info(sLog, "Unknown message ID: %u", msg->msgh_id);
    }
}

} // namespace flux
