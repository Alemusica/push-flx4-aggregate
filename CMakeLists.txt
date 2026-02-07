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

# ---- Subdirectories ----
add_subdirectory(shared)
add_subdirectory(plugin)
add_subdirectory(helper)

# ---- Dev install: plugin + helper + LaunchAgent ----
add_custom_target(install_all
    COMMAND sudo rm -rf "/Library/Audio/Plug-Ins/HAL/PushFLX4Aggregate.driver"
    COMMAND sudo cp -R "$<TARGET_BUNDLE_DIR:PushFLX4Plugin>"
        "/Library/Audio/Plug-Ins/HAL/PushFLX4Aggregate.driver"
    COMMAND sudo chown -R root:wheel
        "/Library/Audio/Plug-Ins/HAL/PushFLX4Aggregate.driver"
    COMMAND sudo cp "$<TARGET_FILE:PushFLX4Helper>" "/usr/local/bin/pushflx4-helper"
    COMMAND sudo chown root:wheel "/usr/local/bin/pushflx4-helper"
    COMMAND cp "${CMAKE_CURRENT_SOURCE_DIR}/com.pushflx4.helper.plist"
        "$ENV{HOME}/Library/LaunchAgents/com.pushflx4.helper.plist"
    COMMAND launchctl bootout "gui/$$(id -u)/com.pushflx4.helper" 2>/dev/null || true
    COMMAND launchctl bootstrap "gui/$$(id -u)"
        "$ENV{HOME}/Library/LaunchAgents/com.pushflx4.helper.plist"
    COMMAND sudo killall coreaudiod || true
    DEPENDS PushFLX4Plugin PushFLX4Helper
    COMMENT "Installing plugin + helper + LaunchAgent, restarting coreaudiod"
)
