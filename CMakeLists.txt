cmake_minimum_required(VERSION 3.18)

project(eqmdsk VERSION 0.9.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(EQMDSK_BUILD_PYTHON "Build the Python extension" ON)
option(EQMDSK_BUILD_TESTS "Build C++ tests" OFF)

set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Eigen3 3.3 QUIET NO_MODULE)
if(NOT TARGET Eigen3::Eigen)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/extern/eigen/Eigen/Core")
    add_library(Eigen3::Eigen INTERFACE IMPORTED GLOBAL)
    set_target_properties(Eigen3::Eigen PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/extern/eigen"
    )
  else()
    message(FATAL_ERROR
      "Eigen3 was not found. Install Eigen3 or provide extern/eigen.")
  endif()
endif()

add_library(eqmdsk
  src/cocos.cpp
  src/error.cpp
  src/field.cpp
  src/fortran.cpp
  src/gfile.cpp
)
add_library(eqmdsk::eqmdsk ALIAS eqmdsk)

target_compile_features(eqmdsk PUBLIC cxx_std_17)
target_link_libraries(eqmdsk PUBLIC Eigen3::Eigen)
target_include_directories(eqmdsk
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)
set_target_properties(eqmdsk PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  VERSION "${PROJECT_VERSION}"
  SOVERSION "0"
)

if(MSVC)
  target_compile_options(eqmdsk PRIVATE /W4 /permissive-)
else()
  target_compile_options(eqmdsk PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(EQMDSK_BUILD_PYTHON)
  find_package(pybind11 CONFIG QUIET)
  if(NOT TARGET pybind11::module)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/extern/pybind11/CMakeLists.txt")
      add_subdirectory(extern/pybind11 EXCLUDE_FROM_ALL)
    else()
      message(FATAL_ERROR
        "pybind11 was not found. Install pybind11 or provide extern/pybind11.")
    endif()
  endif()

  pybind11_add_module(_core MODULE bindings/module.cpp)
  target_link_libraries(_core PRIVATE eqmdsk::eqmdsk)
  target_compile_features(_core PRIVATE cxx_std_17)
  install(TARGETS _core LIBRARY DESTINATION eqmdsk RUNTIME DESTINATION eqmdsk)
endif()

if(EQMDSK_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests/cpp)
endif()

install(TARGETS eqmdsk
  EXPORT eqmdskTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

configure_package_config_file(
  cmake/eqmdskConfig.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/eqmdskConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/eqmdsk"
)
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/eqmdskConfigVersion.cmake"
  VERSION "${PROJECT_VERSION}"
  COMPATIBILITY SameMajorVersion
)
install(EXPORT eqmdskTargets
  FILE eqmdskTargets.cmake
  NAMESPACE eqmdsk::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/eqmdsk"
)
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/eqmdskConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/eqmdskConfigVersion.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/eqmdsk"
)
