cmake_minimum_required(VERSION 3.13)



set(TARGET_NAME  test_net_common)


add_executable(${TARGET_NAME}
  test_common.cpp
  )
target_link_libraries(${TARGET_NAME} PRIVATE gtest gtest_main)


add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME})
