# Make library
project(RSLib CXX)

file(GLOB_RECURSE HEADER_LIST "*.hpp")
file(GLOB_RECURSE SOURCE_LIST "*.cpp")

add_library(RSLib STATIC ${HEADER_LIST} ${SOURCE_LIST})
set_target_properties(RSLib PROPERTIES LINKER_LANGUAGE CXX)
