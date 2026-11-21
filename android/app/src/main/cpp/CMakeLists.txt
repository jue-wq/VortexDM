cmake_minimum_required(VERSION 3.28)
project(vortex_android LANGUAGES CXX)

# Compile the platform-independent URL policy directly from the shared core.
# Android does not yet package the desktop transport/storage dependencies.
set(VORTEX_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../..")
add_library(vortex_android SHARED
    native_bridge.cpp
    "${VORTEX_ROOT}/src/core/network/http_request_policy.cpp"
)
target_include_directories(vortex_android PRIVATE "${VORTEX_ROOT}/src")
target_compile_features(vortex_android PRIVATE cxx_std_23)
target_compile_options(vortex_android PRIVATE -Wall -Wextra -Wpedantic)
