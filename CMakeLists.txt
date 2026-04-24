cmake_minimum_required(VERSION 3.20)
project(cpp_status_server LANGUAGES CXX)

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

add_subdirectory("ThreadPool dll")

add_executable(server main.cpp)
target_link_libraries(server PRIVATE 
  httplib::httplib 
  ThreadPool::ThreadPool
  nlohmann_json::nlohmann_json
)

include(CTest)
if(BUILD_TESTING)
  add_executable(threadpool_smoke_test tests/threadpool_smoke.cpp)
  target_link_libraries(threadpool_smoke_test PRIVATE ThreadPool::ThreadPool)
  add_test(NAME threadpool_smoke COMMAND threadpool_smoke_test)
endif()