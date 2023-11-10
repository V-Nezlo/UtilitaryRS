cmake_minimum_required(VERSION 3.9.0)
project(UtilitaryRS CXX)

include(CTest)
enable_testing()

add_executable(UtilitaryRS main.cpp)

include_directories("Lib")
add_subdirectory(Lib)

include_directories("Test")
add_subdirectory(Test)

target_link_libraries(UtilitaryRS RSLib)
