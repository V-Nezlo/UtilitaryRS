cmake_minimum_required(VERSION 3.9.0)
project(UtilitaryRS VERSION 2.0 LANGUAGES CXX)

add_library(UtilitaryRS INTERFACE)

option(UTILITARY_RS_TESTING "Testing" OFF)
target_include_directories(UtilitaryRS INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

set(COMMON_FLAGS
    -pedantic
    -Wall
    -Wcast-align
    -Wcast-qual
    -Wconversion
    -Wextra
    -Wshadow
    -Wold-style-cast
    -Woverloaded-virtual
    -Wimplicit-fallthrough
    -Wdouble-promotion
    -Wduplicated-cond
    -fms-extensions
)

set_target_properties(UtilitaryRS PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
)

target_compile_options(${PROJECT_NAME} INTERFACE ${COMMON_FLAGS})

if (UTILITARY_RS_TESTING)
    include(CTest)
    enable_testing()
    add_subdirectory(test)
endif()


