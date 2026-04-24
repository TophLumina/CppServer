cmake_minimum_required(VERSION 3.10)
project(ThreadPool VERSION 0.0.1 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 配置多平台符号隐藏机制 (Linux/Mac)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN YES)

# 查找线程库 (pthread 等)
find_package(Threads REQUIRED)

# 创建动态库 (SHARED)
add_library(ThreadPool SHARED
    ThreadPool.cpp
    ThreadPool.h
)

# 链接线程库
target_link_libraries(ThreadPool PUBLIC Threads::Threads)

# 在源码树和安装树都暴露头文件目录
target_include_directories(ThreadPool PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# 便于在 FetchContent/add_subdirectory 场景下使用统一命名空间目标
add_library(ThreadPool::ThreadPool ALIAS ThreadPool)

set(THREADPOOL_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/ThreadPool")

install(TARGETS ThreadPool
    EXPORT ThreadPoolTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}   # Windows 下的 .dll 文件
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}   # Windows 下的 .lib (导入库) 或者 Linux 下的 .so
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}   # 静态库 / 导入库
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(FILES ThreadPool.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}       # 头文件
)

install(EXPORT ThreadPoolTargets
    FILE ThreadPoolTargets.cmake
    NAMESPACE ThreadPool::
    DESTINATION ${THREADPOOL_INSTALL_CMAKEDIR}
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/ThreadPoolConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ThreadPoolConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/ThreadPoolConfig.cmake"
    INSTALL_DESTINATION ${THREADPOOL_INSTALL_CMAKEDIR}
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/ThreadPoolConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/ThreadPoolConfigVersion.cmake"
    DESTINATION ${THREADPOOL_INSTALL_CMAKEDIR}
)
