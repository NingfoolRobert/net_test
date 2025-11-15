cmake_minimum_required(VERSION 3.12)

set(TARGET_NAME test_basic)

file(GLOB_RECURSE SRC_FILES
  LIST_DIRECTORIES false
  CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.c*
)
message(STATUS "${SRC_FILES}")
#add_executable(test_logging test_log.cpp)

#
# list(REMOVE_ITEM ${SRC_FILES}
#   "test_log.cpp"
# )
#file(REMOVE ${SRC_FILES} "test_logging.cpp")
add_executable(${TARGET_NAME}
  ${SRC_FILES}
  # test_fstring.cpp
  # test_bheap.cpp
  # test_staging_buffer.cpp
  # test_shm.cpp
  # test_fbuffer.cpp
  # test_raii.cpp
  # test_fixed_bit_map.cpp
)
target_link_libraries(${TARGET_NAME} PRIVATE gtest gtest_main)


add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME})

