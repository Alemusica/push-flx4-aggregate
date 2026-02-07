# Shared headers — interface library, no compiled sources.
# Both plugin and helper link against this.
add_library(flux_shared INTERFACE)
target_include_directories(flux_shared INTERFACE include)
