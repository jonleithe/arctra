cmake_minimum_required(VERSION 3.20)

project(
    arctra
    VERSION 0.1.0
    DESCRIPTION "Educational SLR satellite pass predictor"
    LANGUAGES CXX
)

option(ARCTRA_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)

find_package(CURL REQUIRED)

add_library(
    arctra_core
    src/coordinates.cpp
    src/cpf_acquisition.cpp
    src/cpf.cpp
    src/observatory.cpp
    src/observatory_file.cpp
    src/pass_finder.cpp
    src/satellite.cpp
    src/satellite_file.cpp
)
target_link_libraries(arctra_core PRIVATE CURL::libcurl)
add_library(arctra::core ALIAS arctra_core)

target_include_directories(
    arctra_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_features(arctra_core PUBLIC cxx_std_20)
set_target_properties(
    arctra_core
    PROPERTIES
        CXX_EXTENSIONS OFF
        CXX_STANDARD_REQUIRED ON
)
arctra_set_project_warnings(arctra_core)

add_executable(arctra main.cpp)
target_link_libraries(arctra PRIVATE arctra::core)
set_target_properties(
    arctra
    PROPERTIES
        CXX_EXTENSIONS OFF
        CXX_STANDARD_REQUIRED ON
)

arctra_set_project_warnings(arctra)

include(CTest)
if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    add_subdirectory(tests)
endif()
