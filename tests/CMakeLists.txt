add_executable(vortex_core_tests
  test_core.cpp
  ../src/native_host/native_messaging_host.cpp
)
target_include_directories(vortex_core_tests PRIVATE ../src/native_host)
target_link_libraries(vortex_core_tests PRIVATE vortex_core)
add_test(NAME vortex_core_tests COMMAND vortex_core_tests)

if(NOT ANDROID)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  add_test(
    NAME vortex_native_host_process_e2e
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/native_host_e2e.py $<TARGET_FILE:vortex-native-host>
  )
endif()
