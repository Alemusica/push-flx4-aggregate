#include "MachClient.h"
#include "Constants.h"

#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <servers/bootstrap.h>
#include <os/log.h>
#include <cstring>

namespace flux {

static os_log_t sLog = os_log_create("com.pushflx4.aggregate.plugin", "MachClient");

// Message structures matching the helper's protocol.
struct RequestMsg {
    mach_msg_header_t header;
};

struct ReplyMsg {
    mach_msg_header_t header;
    mach_msg_body_t body;
    mach_msg_port_descriptor_t memoryPort;
    mach_vm_size_t memorySize;
    mach_msg_trailer_t trailer;
};

MachClient::~MachClient()
{
    disconnect();
}

bool MachClient::connect()
{
    if (sharedMem_) return true;

    // Look up the helper's Mach service.
    mach_port_t servicePort = MACH_PORT_NULL;
    kern_return_t kr = bootstrap_look_up(
        bootstrap_port, kMachServiceName, &servicePort);
    if (kr != KERN_SUCCESS) {
        os_log_error(sLog, "bootstrap_look_up failed for '%{public}s': %s — is the helper running?",
                     kMachServiceName, mach_error_string(kr));
        return false;
    }

    // Create a reply port for receiving the memory entry.
    mach_port_t replyPort = MACH_PORT_NULL;
    kr = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &replyPort);
    if (kr != KERN_SUCCESS) {
        os_log_error(sLog, "mach_port_allocate failed: %s", mach_error_string(kr));
        mach_port_deallocate(mach_task_self(), servicePort);
        return false;
    }

    // Send kMsgRequestMemory to the helper.
    RequestMsg request;
    std::memset(&request, 0, sizeof(request));
    request.header.msgh_bits =
        MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, MACH_MSG_TYPE_MAKE_SEND_ONCE);
    request.header.msgh_size = sizeof(request);
    request.header.msgh_remote_port = servicePort;
    request.header.msgh_local_port = replyPort;
    request.header.msgh_id = kMsgRequestMemory;

    kr = mach_msg(
        &request.header,
        MACH_SEND_MSG | MACH_SEND_TIMEOUT,
        sizeof(request),
        0, MACH_PORT_NULL,
        2000,   // 2s timeout
        MACH_PORT_NULL);

    if (kr != MACH_MSG_SUCCESS) {
        os_log_error(sLog, "Failed to send memory request: %s", mach_error_string(kr));
        mach_port_deallocate(mach_task_self(), replyPort);
        mach_port_deallocate(mach_task_self(), servicePort);
        return false;
    }

    // Receive the reply with the memory port.
    uint8_t replyBuf[sizeof(ReplyMsg) + 256];
    std::memset(replyBuf, 0, sizeof(replyBuf));
    auto* reply = reinterpret_cast<mach_msg_header_t*>(replyBuf);
    reply->msgh_size = sizeof(replyBuf);
    reply->msgh_local_port = replyPort;

    kr = mach_msg(
        reply,
        MACH_RCV_MSG | MACH_RCV_TIMEOUT,
        0,
        sizeof(replyBuf),
        replyPort,
        5000,   // 5s timeout
        MACH_PORT_NULL);

    mach_port_deallocate(mach_task_self(), replyPort);
    mach_port_deallocate(mach_task_self(), servicePort);

    if (kr != MACH_MSG_SUCCESS) {
        os_log_error(sLog, "Failed to receive memory reply: %s", mach_error_string(kr));
        return false;
    }

    auto* replyMsg = reinterpret_cast<ReplyMsg*>(replyBuf);
    if (replyMsg->header.msgh_id != kMsgMemoryReply) {
        os_log_error(sLog, "Unexpected reply ID: %u", replyMsg->header.msgh_id);
        return false;
    }

    // Map the shared memory into our address space.
    mach_port_t memPort = replyMsg->memoryPort.name;
    mach_vm_size_t memSize = replyMsg->memorySize;

    mach_vm_address_t addr = 0;
    kr = mach_vm_map(
        mach_task_self(),
        &addr,
        memSize,
        0,          // alignment mask
        VM_FLAGS_ANYWHERE,
        memPort,
        0,          // offset
        FALSE,      // copy — FALSE = share the pages
        VM_PROT_READ | VM_PROT_WRITE,
        VM_PROT_READ | VM_PROT_WRITE,
        VM_INHERIT_NONE);

    mach_port_deallocate(mach_task_self(), memPort);

    if (kr != KERN_SUCCESS) {
        os_log_error(sLog, "mach_vm_map failed: %s", mach_error_string(kr));
        return false;
    }

    mappedAddr_ = addr;
    mappedSize_ = memSize;
    sharedMem_ = reinterpret_cast<SharedMemoryLayout*>(addr);

    os_log_info(sLog, "Shared memory mapped: %llu bytes at %p", memSize, sharedMem_);
    return true;
}

void MachClient::disconnect()
{
    if (mappedAddr_ != 0) {
        mach_vm_deallocate(mach_task_self(), mappedAddr_, mappedSize_);
        mappedAddr_ = 0;
        mappedSize_ = 0;
        sharedMem_ = nullptr;
    }
}

} // namespace flux
