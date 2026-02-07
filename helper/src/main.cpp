#include "AudioEngine.h"
#include "MachServer.h"
#include "Constants.h"

#include <os/log.h>
#include <CoreFoundation/CoreFoundation.h>
#include <csignal>
#include <thread>

static os_log_t sLog = os_log_create("com.pushflx4.aggregate.helper", "main");

static std::atomic<bool> gShouldQuit{false};

static void signalHandler(int sig)
{
    os_log_info(sLog, "Signal %d received, shutting down", sig);
    gShouldQuit.store(true, std::memory_order_relaxed);
    CFRunLoopStop(CFRunLoopGetMain());
}

int main(int argc, const char* argv[])
{
    os_log_info(sLog, "PushFLX4 helper daemon starting");

    // ---- Device UIDs ----
    // TODO: read from config file or command-line args.
    // For now, use placeholders — replace with actual UIDs from:
    //   system_profiler SPAudioDataType
    std::string pushUID = "AppleUSBAudioEngine:Ableton:Push 2:PLACEHOLDER";
    std::string flx4UID = "AppleUSBAudioEngine:Pioneer:DDJ-FLX4:PLACEHOLDER";

    // Override from command line: --push-uid <uid> --flx4-uid <uid>
    for (int i = 1; i < argc - 1; ++i) {
        if (std::string(argv[i]) == "--push-uid") {
            pushUID = argv[++i];
        } else if (std::string(argv[i]) == "--flx4-uid") {
            flx4UID = argv[++i];
        }
    }

    // ---- Signal handling ----
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // ---- Mach IPC server ----
    flux::MachServer server;
    if (!server.start()) {
        os_log_error(sLog, "Failed to start Mach server — exiting");
        return 1;
    }

    // ---- Audio engine ----
    flux::AudioEngine engine(server.sharedMemory(), pushUID, flx4UID);
    if (!engine.start()) {
        os_log_error(sLog, "Failed to start audio engine — exiting");
        return 1;
    }

    // ---- Mach message loop on a background thread ----
    std::thread machThread([&server]() {
        server.runMessageLoop();
    });

    os_log_info(sLog, "Helper daemon running — waiting for plugin connections");

    // ---- Main run loop (needed for CoreAudio callbacks + IOKit notifications) ----
    while (!gShouldQuit.load(std::memory_order_relaxed)) {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1.0, true);
    }

    // ---- Shutdown ----
    os_log_info(sLog, "Shutting down");
    engine.stop();
    server.requestStop();
    if (machThread.joinable()) machThread.join();
    server.stop();

    os_log_info(sLog, "Helper daemon exited cleanly");
    return 0;
}
