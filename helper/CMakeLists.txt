# Companion helper daemon — runs as LaunchAgent outside coreaudiod sandbox.
# Opens IOProcs on real hardware, manages drift + resampling, serves shared memory.

add_executable(PushFLX4Helper
    src/main.cpp
    src/HardwareDevice.cpp
    src/MachServer.cpp
    src/AudioEngine.cpp
    src/ProcessTap.mm
)

set_target_properties(PushFLX4Helper PROPERTIES
    OUTPUT_NAME "pushflx4-helper"
)

# Enable ObjC++ for .mm files (ProcessTap uses CATapDescription ObjC API).
set_source_files_properties(src/ProcessTap.mm PROPERTIES
    COMPILE_FLAGS "-x objective-c++ -fobjc-arc"
)

target_link_libraries(PushFLX4Helper PRIVATE
    flux_shared
    samplerate
    "-framework CoreAudio"
    "-framework CoreFoundation"
    "-framework IOKit"
    "-framework AudioToolbox"
    "-framework Foundation"
)

target_compile_options(PushFLX4Helper PRIVATE
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
)
