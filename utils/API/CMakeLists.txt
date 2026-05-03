include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CPPSERVER_API_VERSION "0.0.1")
set(CPPSERVER_API_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/CppServerAPI")

add_library(CppServerAPI INTERFACE)
set_target_properties(CppServerAPI PROPERTIES EXPORT_NAME API)
add_library(httplib::API ALIAS CppServerAPI)

# Generate a dedicated compile command for API.h so clangd can index it
# without introducing an extra translation unit source file.
set(CPPSERVER_API_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/API.h")
set_source_files_properties(${CPPSERVER_API_HEADER} PROPERTIES
  LANGUAGE CXX
  HEADER_FILE_ONLY OFF
)

add_library(CppServerAPIIndex OBJECT EXCLUDE_FROM_ALL
  ${CPPSERVER_API_HEADER}
)
target_link_libraries(CppServerAPIIndex PRIVATE CppServerAPI)

target_include_directories(CppServerAPI INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/CppServer/API>
)

target_link_libraries(CppServerAPI INTERFACE
  $<BUILD_INTERFACE:httplib::httplib>
  $<BUILD_INTERFACE:nlohmann_json::nlohmann_json>
)

install(TARGETS CppServerAPI
  EXPORT CppServerAPITargets
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/CppServer/API
)

install(FILES API.h
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/CppServer/API
)

install(EXPORT CppServerAPITargets
  FILE CppServerAPITargets.cmake
  NAMESPACE httplib::
  DESTINATION ${CPPSERVER_API_INSTALL_CMAKEDIR}
)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/CppServerAPIConfigVersion.cmake"
  VERSION ${CPPSERVER_API_VERSION}
  COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CppServerAPIConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/CppServerAPIConfig.cmake"
  INSTALL_DESTINATION ${CPPSERVER_API_INSTALL_CMAKEDIR}
)

install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/CppServerAPIConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/CppServerAPIConfigVersion.cmake"
  DESTINATION ${CPPSERVER_API_INSTALL_CMAKEDIR}
)
