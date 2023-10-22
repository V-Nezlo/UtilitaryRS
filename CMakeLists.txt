cmake_minimum_required(VERSION 3.9.0)
project(UtilitaryRS VERSION 0.1.0) 

include(CTest)
enable_testing()

add_executable(UtilitaryRS main.cpp)

include_directories("Lib")
add_subdirectory(Lib)

include_directories("Test")
add_subdirectory(Test)

target_link_libraries(UtilitaryRS RSLib)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
