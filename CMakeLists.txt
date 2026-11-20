cmake_minimum_required(VERSION 3.28)

project(VortexDM
    VERSION 0.1.0
    DESCRIPTION "Modern C++23 cross-platform download manager"
    LANGUAGES CXX
)

include(GNUInstallDirs)

option(VORTEXDM_BUILD_TESTS "Build VortexDM tests" ON)
option(VORTEXDM_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

find_package(OpenSSL REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_library(vortex_core STATIC
    src/core/common/crypto_utils.cpp
    src/core/common/logger.cpp
    src/core/storage/sqlite_database.cpp
    src/core/storage/offset_file_writer.cpp
    src/core/download/segment_planner.cpp
    src/core/download/retry_policy.cpp
    src/core/download/content_range.cpp
    src/core/ipc/native_messaging_frame.cpp
    src/core/ipc/native_messaging_stream.cpp
    src/core/ipc/native_batch_assembler.cpp
    src/core/ipc/native_message_dispatcher.cpp
)
add_library(VortexDM::Core ALIAS vortex_core)

add_executable(vortexdm_native_host src/native_host/main.cpp)
target_compile_features(vortexdm_native_host PRIVATE cxx_std_23)
target_link_libraries(vortexdm_native_host PRIVATE VortexDM::Core)

install(TARGETS vortexdm_native_host RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
set(VORTEXDM_NATIVE_HOST_INSTALL_PATH "${CMAKE_INSTALL_FULL_BINDIR}/vortexdm_native_host")
configure_file(
    packaging/firefox/com.vortexdm.native_host.json.in
    ${CMAKE_CURRENT_BINARY_DIR}/com.vortexdm.native_host.json
    @ONLY
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/com.vortexdm.native_host.json
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/mozilla/native-messaging-hosts)

target_compile_features(vortex_core PUBLIC cxx_std_23)
target_include_directories(vortex_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)
target_link_libraries(vortex_core
    PUBLIC
        OpenSSL::Crypto
        spdlog::spdlog
        unofficial::sqlite3::sqlite3
        nlohmann_json::nlohmann_json
)

if(MSVC)
    target_compile_options(vortex_core PRIVATE /W4 /permissive- /Zc:__cplusplus)
else()
    target_compile_options(vortex_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

if(VORTEXDM_ENABLE_SANITIZERS AND NOT MSVC)
    target_compile_options(vortex_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(vortex_core PUBLIC -fsanitize=address,undefined)
endif()

if(VORTEXDM_BUILD_TESTS)
    enable_testing()
    find_package(Catch2 3 CONFIG REQUIRED)
    add_executable(vortex_unit_tests
        tests/unit/crypto_utils_tests.cpp
        tests/unit/sqlite_database_tests.cpp
        tests/unit/offset_file_writer_tests.cpp
        tests/unit/segment_planner_tests.cpp
        tests/unit/retry_policy_tests.cpp
        tests/unit/content_range_tests.cpp
        tests/unit/download_state_machine_tests.cpp
        tests/unit/native_messaging_frame_tests.cpp
        tests/unit/native_batch_assembler_tests.cpp
        tests/unit/native_message_dispatcher_tests.cpp
    )
    target_link_libraries(vortex_unit_tests
        PRIVATE
            VortexDM::Core
            Catch2::Catch2WithMain
    )
    include(CTest)
    include(Catch)
    catch_discover_tests(vortex_unit_tests)
endif()
