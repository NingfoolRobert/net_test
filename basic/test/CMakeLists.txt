cmake_minimum_required(VERSION 3.12)



set(TARGET_NAME  test_basic)


add_executable(${TARGET_NAME} test_fstring.cpp
  test_bheap.cpp)
target_link_libraries(${TARGET_NAME} PRIVATE gtest gtest_main)


enable_testing()
add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME}  ${PROJECT_NAME}_test  --gtest_filter=fstring)
