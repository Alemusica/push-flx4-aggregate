# HAL AudioServerPlugin — runs inside coreaudiod.
# No CoreAudio client API calls. Reads/writes shared memory only.

add_library(PushFLX4Plugin MODULE
    src/PluginEntry.cpp
    src/PluginHandler.cpp
    src/MachClient.cpp
)

set_target_properties(PushFLX4Plugin PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "driver"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/src/Info.plist.in"
    MACOSX_BUNDLE_BUNDLE_NAME "PushFLX4Aggregate"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.pushflx4.aggregate.plugin"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    OUTPUT_NAME "PushFLX4Aggregate"
    PREFIX ""
    SUFFIX ""
)

target_link_libraries(PushFLX4Plugin PRIVATE
    flux_shared
    aspl::libASPL
    "-framework CoreFoundation"
)

target_compile_options(PushFLX4Plugin PRIVATE
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
)
