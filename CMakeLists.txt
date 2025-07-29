cmake_minimum_required(VERSION 3.9.0)
project(UtilRS CXX)

include(CTest)
enable_testing()

add_executable(UtilRS main.cpp)

add_definitions(-DRSLIB_TESTING)

include_directories("UtilitaryRS")
add_subdirectory(UtilitaryRS)

include_directories("Test")
add_subdirectory(Test)

target_link_libraries(UtilRS RSLib)
