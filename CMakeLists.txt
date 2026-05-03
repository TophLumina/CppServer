cmake_minimum_required(VERSION 3.20)
project(cpp_status_server LANGUAGES CXX)

if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(
  httplib
  URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.15.3.tar.gz
)
FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.tar.gz
)
FetchContent_MakeAvailable(httplib)
FetchContent_MakeAvailable(nlohmann_json)

add_subdirectory("utils/ThreadPool dll")
add_subdirectory("utils/API")

file(GLOB_RECURSE SERVICE_HEADERS CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/services/*.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/services/*.hpp"
)

add_executable(server 
  main.cpp
  Core.h
  Utils.h
  Core.cpp
  ${SERVICE_HEADERS}
)
target_link_libraries(server PRIVATE 
  httplib::API
  httplib::httplib 
  ThreadPool::ThreadPool
  nlohmann_json::nlohmann_json
)

# On Windows, copy the ThreadPool DLL to the output directory of the server executable so that it can find the DLL at runtime.
if(WIN32)
  add_custom_command(TARGET server POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:ThreadPool>
      $<TARGET_FILE_DIR:server>
  )
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(threadpool_smoke_test tests/threadpool_smoke.cpp)
  target_link_libraries(threadpool_smoke_test PRIVATE ThreadPool::ThreadPool)

  # On Windows, copy the ThreadPool DLL to the output directory of the test executable so that it can find the DLL at runtime.
  if(WIN32)
    add_custom_command(TARGET threadpool_smoke_test POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:ThreadPool>
        $<TARGET_FILE_DIR:threadpool_smoke_test>
    )
  endif()

  add_test(NAME threadpool_smoke COMMAND threadpool_smoke_test)
endif()