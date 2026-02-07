cmake_minimum_required(VERSION 3.18)
project(PushFLX4Aggregate VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "M1 Max target")
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS")

# ---- Dependencies ----
include(FetchContent)

FetchContent_Declare(libASPL
    GIT_REPOSITORY https://github.com/gavv/libASPL.git
    GIT_TAG        main
)
FetchContent_MakeAvailable(libASPL)

FetchContent_Declare(libsamplerate
    GIT_REPOSITORY https://github.com/libsndfile/libsamplerate.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(libsamplerate)

# ---- Driver Target ----
add_library(PushFLX4Aggregate MODULE
    src/PluginEntry.cpp
    src/AggregateDevice.cpp
    src/AggregateHandler.cpp
    src/HardwareDevice.cpp
    src/TPCircularBuffer.c
)

set_target_properties(PushFLX4Aggregate PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "driver"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/src/Info.plist.in"
    MACOSX_BUNDLE_BUNDLE_NAME "PushFLX4Aggregate"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.custom.audio.PushFLX4Aggregate"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    OUTPUT_NAME "PushFLX4Aggregate"
    PREFIX ""
    SUFFIX ""
)

target_include_directories(PushFLX4Aggregate PRIVATE src)

target_link_libraries(PushFLX4Aggregate PRIVATE
    aspl::libASPL
    samplerate
    "-framework CoreAudio"
    "-framework CoreFoundation"
    "-framework IOKit"
    "-framework AudioToolbox"
)

target_compile_options(PushFLX4Aggregate PRIVATE
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
)

# ---- Install target ----
install(TARGETS PushFLX4Aggregate
    BUNDLE DESTINATION "/Library/Audio/Plug-Ins/HAL"
)

# ---- Quick dev install ----
add_custom_target(install_driver
    COMMAND sudo rm -rf "/Library/Audio/Plug-Ins/HAL/PushFLX4Aggregate.driver"
    COMMAND sudo cp -R "$<TARGET_BUNDLE_DIR:PushFLX4Aggregate>"
        "/Library/Audio/Plug-Ins/HAL/PushFLX4Aggregate.driver"
    COMMAND sudo chown -R root:wheel
        "/Library/Audio/Plug-Ins/HAL/PushFLX4Aggregate.driver"
    COMMAND sudo killall coreaudiod || true
    DEPENDS PushFLX4Aggregate
    COMMENT "Installing driver and restarting coreaudiod"
)
