cmake_minimum_required(VERSION 3.25)
project(VortexDM VERSION 0.1.0 LANGUAGES CXX)

if(APPLE)
  message(FATAL_ERROR "VortexDM does not support macOS; supported targets are Windows, Linux and Android.")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(VORTEX_BUILD_UI "Build the optional Qt 6 desktop/Android UI" OFF)
option(VORTEX_BUILD_TESTS "Build unit tests" ON)
option(VORTEX_ENABLE_ASAN_UBSAN "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(VORTEX_ENABLE_TSAN "Enable ThreadSanitizer" OFF)

if(VORTEX_ENABLE_ASAN_UBSAN AND VORTEX_ENABLE_TSAN)
  message(FATAL_ERROR "ASan/UBSan and TSan must be enabled in separate builds")
endif()
if(VORTEX_ENABLE_ASAN_UBSAN OR VORTEX_ENABLE_TSAN)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR "Vortex sanitizers require GCC or Clang")
  endif()
  if(VORTEX_ENABLE_ASAN_UBSAN)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
  else()
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
  endif()
endif()

find_package(CURL REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(tinyxml2 CONFIG REQUIRED)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
find_package(Threads REQUIRED)

add_library(vortex_core
  src/core/src/url.cpp
  src/core/src/task.cpp
  src/core/src/download_engine.cpp
  src/core/src/download_queue_worker.cpp
  src/core/src/application_paths.cpp
  src/core/src/sqlite_repository.cpp
  src/core/src/crash_safe_file_writer.cpp
  src/core/src/segment_scheduler.cpp
  src/core/src/filename_sanitizer.cpp
  src/core/src/chunk_assembler.cpp
  src/core/src/process_runner.cpp
  src/core/src/ytdlp_extractor.cpp
  src/core/src/segment_checkpoint.cpp
  src/core/src/segment_recovery.cpp
  src/core/src/ytdlp_toolchain.cpp
  src/core/src/media_muxer.cpp
  src/core/src/stream_manifest.cpp
)
target_include_directories(vortex_core PUBLIC src/core/include)
# CMake 4.3 renamed the FindSQLite3 imported target; keep compatibility with older CMake.
if(TARGET SQLite3::SQLite3)
  set(VORTEX_SQLITE_TARGET SQLite3::SQLite3)
else()
  set(VORTEX_SQLITE_TARGET SQLite::SQLite3)
endif()
target_link_libraries(vortex_core PUBLIC CURL::libcurl ${VORTEX_SQLITE_TARGET} nlohmann_json::nlohmann_json OpenSSL::Crypto Threads::Threads tinyxml2::tinyxml2)
target_compile_features(vortex_core PUBLIC cxx_std_23)
if(MSVC)
  target_compile_options(vortex_core PRIVATE /W4 /permissive-)
else()
  target_compile_options(vortex_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

add_executable(vortex-cli src/cli/main.cpp)
target_link_libraries(vortex-cli PRIVATE vortex_core)

# Native Messaging hosts are desktop executables. Android Firefox uses the opt-in loopback bridge instead.
if(NOT ANDROID)
  add_executable(vortex-native-host src/native_host/main.cpp src/native_host/native_messaging_host.cpp)
  target_include_directories(vortex-native-host PRIVATE src/native_host)
  target_link_libraries(vortex-native-host PRIVATE vortex_core)
endif()

if(VORTEX_BUILD_UI)
  find_package(Qt6 6.8 REQUIRED COMPONENTS Quick Network)
  qt_standard_project_setup()
  if(ANDROID)
    qt_add_executable(vortexdm src/ui/main.cpp src/ui/download_list_model.cpp src/ui/android_loopback_server.cpp)
  else()
    qt_add_executable(vortexdm src/ui/main.cpp src/ui/download_list_model.cpp)
  endif()
  qt_add_qml_module(vortexdm URI VortexDM VERSION 1.0 QML_FILES src/ui/Main.qml)
  target_link_libraries(vortexdm PRIVATE Qt6::Quick Qt6::Network vortex_core)
  if(ANDROID)
    target_include_directories(vortexdm PRIVATE src/ui)
  endif()
  if(ANDROID)
    set_property(TARGET vortexdm PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    set_property(TARGET vortexdm PROPERTY QT_ANDROID_MIN_SDK_VERSION 28)
    set_property(TARGET vortexdm PROPERTY QT_ANDROID_TARGET_SDK_VERSION 35)
    set_property(TARGET vortexdm PROPERTY QT_ANDROID_PACKAGE_NAME "com.vortexdm.app")
  endif()
endif()

if(VORTEX_BUILD_TESTS AND NOT ANDROID)
  enable_testing()
  add_subdirectory(tests)
endif()

install(TARGETS vortex_core vortex-cli
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
)
if(NOT ANDROID)
  install(TARGETS vortex-native-host RUNTIME DESTINATION bin)
endif()
if(VORTEX_BUILD_UI AND NOT ANDROID)
  install(TARGETS vortexdm RUNTIME DESTINATION bin)
endif()
