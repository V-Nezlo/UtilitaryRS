# Включаем директорию с тестами
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# Функция для создания отдельных тестов
function(add_unit_test TEST_NAME SOURCE_FILE)
    add_executable(${TEST_NAME} ${SOURCE_FILE})
    target_link_libraries(${TEST_NAME} PRIVATE UtilitaryRS)
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

# Создаем отдельный исполняемый файл для каждого теста
file(GLOB TEST_SOURCES "*.cpp")

foreach(TEST_SOURCE ${TEST_SOURCES})
    get_filename_component(TEST_NAME ${TEST_SOURCE} NAME_WE)
    add_unit_test(${TEST_NAME} ${TEST_SOURCE})
endforeach()
