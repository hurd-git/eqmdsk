cmake_minimum_required(VERSION 3.18)

project(eqmdsk VERSION 0.9.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

if(SKBUILD)
  set(_eqmdsk_python_default ON)
else()
  set(_eqmdsk_python_default OFF)
endif()
option(EQMDSK_BUILD_PYTHON
  "Build the Python extension"
  ${_eqmdsk_python_default}
)
unset(_eqmdsk_python_default)
option(EQMDSK_BUILD_TESTS "Build C++ tests" OFF)
option(EQMDSK_BUILD_FUZZERS "Build Clang libFuzzer parser targets" OFF)
if(SKBUILD)
  set(_eqmdsk_fetch_default ON)
else()
  set(_eqmdsk_fetch_default OFF)
endif()
option(EQMDSK_FETCH_DEPENDENCIES
  "Fetch missing build dependencies from pinned upstream releases"
  ${_eqmdsk_fetch_default}
)
unset(_eqmdsk_fetch_default)

set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Eigen3 QUIET NO_MODULE)
if(Eigen3_FOUND AND
   (NOT DEFINED Eigen3_VERSION OR Eigen3_VERSION VERSION_LESS 3.4))
  message(FATAL_ERROR
    "eqmdsk requires Eigen >= 3.4; found ${Eigen3_VERSION}.")
endif()
if(NOT TARGET Eigen3::Eigen)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/extern/eigen/Eigen/Core")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/extern/eigen/Eigen/Version")
      set(_eqmdsk_eigen_version_file
        "${CMAKE_CURRENT_SOURCE_DIR}/extern/eigen/Eigen/Version"
      )
      file(STRINGS
        "${_eqmdsk_eigen_version_file}"
        _eqmdsk_eigen_version_lines
        REGEX "^#define EIGEN_VERSION_STRING"
      )
      string(REGEX MATCH
        "EIGEN_VERSION_STRING \"([0-9]+[.][0-9]+[.][0-9]+)\""
        _match
        "${_eqmdsk_eigen_version_lines}"
      )
      if(_match STREQUAL "")
        message(FATAL_ERROR
          "Unable to determine the version of extern/eigen.")
      endif()
      set(_eqmdsk_eigen_version "${CMAKE_MATCH_1}")
    else()
      set(_eqmdsk_eigen_version_file
        "${CMAKE_CURRENT_SOURCE_DIR}/extern/eigen/Eigen/src/Core/util/Macros.h"
      )
      file(STRINGS
        "${_eqmdsk_eigen_version_file}"
        _eqmdsk_eigen_version_lines
        REGEX "^#define EIGEN_(WORLD|MAJOR|MINOR)_VERSION [0-9]+"
      )
      foreach(_part IN ITEMS WORLD MAJOR MINOR)
        string(REGEX MATCH
          "#define EIGEN_${_part}_VERSION ([0-9]+)"
          _match
          "${_eqmdsk_eigen_version_lines}"
        )
        if(_match STREQUAL "")
          message(FATAL_ERROR
            "Unable to determine the version of extern/eigen.")
        endif()
        set(_eqmdsk_eigen_${_part} "${CMAKE_MATCH_1}")
      endforeach()
      set(_eqmdsk_eigen_version
        "${_eqmdsk_eigen_WORLD}.${_eqmdsk_eigen_MAJOR}.${_eqmdsk_eigen_MINOR}"
      )
    endif()
    if(_eqmdsk_eigen_version VERSION_LESS 3.4)
      message(FATAL_ERROR
        "eqmdsk requires Eigen >= 3.4; extern/eigen is ${_eqmdsk_eigen_version}.")
    endif()
    unset(_eqmdsk_eigen_version_lines)
    unset(_eqmdsk_eigen_version_file)
    unset(_eqmdsk_eigen_version)
    unset(_eqmdsk_eigen_WORLD)
    unset(_eqmdsk_eigen_MAJOR)
    unset(_eqmdsk_eigen_MINOR)
    unset(_match)
    add_library(Eigen3::Eigen INTERFACE IMPORTED GLOBAL)
    set_target_properties(Eigen3::Eigen PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/extern/eigen"
    )
  elseif(EQMDSK_FETCH_DEPENDENCIES)
    include(FetchContent)
    if(POLICY CMP0135)
      cmake_policy(SET CMP0135 NEW)
    endif()
    FetchContent_Declare(eqmdsk_eigen
      URL "https://gitlab.com/libeigen/eigen/-/archive/5.0.1/eigen-5.0.1.tar.gz"
      URL_HASH
        "SHA256=e9c326dc8c05cd1e044c71f30f1b2e34a6161a3b6ecf445d56b53ff1669e3dec"
      # Eigen is used header-only.  A deliberately absent source subdirectory
      # lets MakeAvailable populate it without configuring Eigen's own project.
      SOURCE_SUBDIR _eqmdsk_header_only
    )
    FetchContent_MakeAvailable(eqmdsk_eigen)
    add_library(Eigen3::Eigen INTERFACE IMPORTED GLOBAL)
    set_target_properties(Eigen3::Eigen PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${eqmdsk_eigen_SOURCE_DIR}"
    )
  else()
    message(FATAL_ERROR
      "Eigen3 was not found. Install Eigen3, provide extern/eigen, or set "
      "EQMDSK_FETCH_DEPENDENCIES=ON.")
  endif()
endif()

add_library(eqmdsk STATIC
  src/afile.cpp
  src/cocos.cpp
  src/error.cpp
  src/field.cpp
  src/fortran.cpp
  src/gfile.cpp
  src/kfile.cpp
  src/namelist.cpp
  src/sfile.cpp
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
  target_compile_options(eqmdsk PRIVATE /W4 /permissive- /bigobj)
else()
  target_compile_options(eqmdsk PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(EQMDSK_BUILD_PYTHON)
  set(PYBIND11_FINDPYTHON ON)
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
  install(TARGETS _core
    LIBRARY DESTINATION eqmdsk COMPONENT Python
    RUNTIME DESTINATION eqmdsk COMPONENT Python
  )
endif()

if(EQMDSK_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests/cpp)
endif()

if(EQMDSK_BUILD_FUZZERS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "EQMDSK_BUILD_FUZZERS requires Clang/libFuzzer")
  endif()
  target_compile_options(eqmdsk PRIVATE
    -fsanitize=fuzzer-no-link,address,undefined
    -fno-omit-frame-pointer
  )
  foreach(_format IN ITEMS G A K S)
    string(TOLOWER "${_format}" _format_lower)
    add_executable("eqmdsk_fuzz_${_format_lower}" tests/fuzz/fuzz_parse.cpp)
    target_link_libraries("eqmdsk_fuzz_${_format_lower}" PRIVATE eqmdsk::eqmdsk)
    target_compile_features("eqmdsk_fuzz_${_format_lower}" PRIVATE cxx_std_17)
    target_compile_definitions("eqmdsk_fuzz_${_format_lower}" PRIVATE
      "EQMDSK_FUZZ_FORMAT_${_format}=1"
    )
    target_compile_options("eqmdsk_fuzz_${_format_lower}" PRIVATE
      -fsanitize=fuzzer,address,undefined
      -fno-omit-frame-pointer
    )
    target_link_options("eqmdsk_fuzz_${_format_lower}" PRIVATE
      -fsanitize=fuzzer,address,undefined
    )
  endforeach()
endif()

install(TARGETS eqmdsk
  EXPORT eqmdskTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT Development
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT Development
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT Development
  INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
install(DIRECTORY include/
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  COMPONENT Development
)
install(FILES LICENSE THIRD_PARTY_NOTICES.md
  DESTINATION "${CMAKE_INSTALL_DOCDIR}"
  COMPONENT Development
)
install(DIRECTORY licenses/
  DESTINATION "${CMAKE_INSTALL_DOCDIR}/licenses"
  COMPONENT Development
)

configure_package_config_file(
  cmake/eqmdskConfig.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/eqmdskConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/eqmdsk"
)
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/eqmdskConfigVersion.cmake"
  VERSION "${PROJECT_VERSION}"
  COMPATIBILITY SameMinorVersion
)
install(EXPORT eqmdskTargets
  FILE eqmdskTargets.cmake
  NAMESPACE eqmdsk::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/eqmdsk"
  COMPONENT Development
)
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/eqmdskConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/eqmdskConfigVersion.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/eqmdsk"
  COMPONENT Development
)
