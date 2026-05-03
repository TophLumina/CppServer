include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(API_VERSION "0.0.1")
set(API_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/API")

add_library(API INTERFACE)
add_library(API::API ALIAS API)
add_library(httplib::API ALIAS API)

# Generate a dedicated compile command for API.h so clangd can index it
# without introducing an extra translation unit source file.
set(API_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/API.h")
set_source_files_properties(${API_HEADER} PROPERTIES
  LANGUAGE CXX
  HEADER_FILE_ONLY OFF
)

add_library(APIIndex OBJECT EXCLUDE_FROM_ALL
  ${API_HEADER}
)
target_link_libraries(APIIndex PRIVATE API)

target_include_directories(API INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(API INTERFACE
  $<BUILD_INTERFACE:httplib::httplib>
  $<BUILD_INTERFACE:nlohmann_json::nlohmann_json>
)

install(TARGETS API
  EXPORT APITargets
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(FILES API.h
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT APITargets
  FILE APITargets.cmake
  NAMESPACE API::
  DESTINATION ${API_INSTALL_CMAKEDIR}
)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/APIConfigVersion.cmake"
  VERSION ${API_VERSION}
  COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/APIConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/APIConfig.cmake"
  INSTALL_DESTINATION ${API_INSTALL_CMAKEDIR}
)

install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/APIConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/APIConfigVersion.cmake"
  DESTINATION ${API_INSTALL_CMAKEDIR}
)
