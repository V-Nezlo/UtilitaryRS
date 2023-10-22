project(Tests)

add_executable(test1 ParserTest.cpp)
add_executable(test2 HandlerTest.cpp)

target_link_libraries(test1 RSLib)
target_link_libraries(test2 RSLib)

add_test(NAME Test1 COMMAND test1)
add_test(NAME Test2 COMMAND test2)
