cmake_minimum_required(VERSION 3.12)



set(TARGET_NAME  test_basic)

add_executable(test_logging test_log.cpp)

add_executable(${TARGET_NAME} test_fstring.cpp
  test_bheap.cpp
  test_staging_buffer.cpp
  test_shm.cpp
  test_fbuffer.cpp
  test_raii.cpp
  )
target_link_libraries(${TARGET_NAME} PRIVATE gtest gtest_main)


add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME})

