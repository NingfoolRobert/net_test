cmake_minimum_required(VERSION 3.13)



set(TARGET_NAME  test_net_common)


add_executable(${TARGET_NAME}
  test_common.cpp
  )
target_link_libraries(${TARGET_NAME} PRIVATE gtest gtest_main)


add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME})

add_executable(test_tcp_stream_reassembly test_tcp_stream_reassembly.cpp)
target_link_libraries(test_tcp_stream_reassembly PRIVATE tcp_reassembly gtest_main)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_sources(test_tcp_stream_reassembly PRIVATE test_tcp_stream_reassembly_allocation.cpp)
  target_link_options(test_tcp_stream_reassembly PRIVATE
    "-Wl,--wrap=posix_memalign" "-Wl,--wrap=free")
endif()
add_test(NAME test_tcp_stream_reassembly COMMAND test_tcp_stream_reassembly)

add_executable(test_tcp_stream_manager test_tcp_stream_manager.cpp
  test_tcp_four_tuple_filter.cpp)
target_link_libraries(test_tcp_stream_manager PRIVATE tcp_reassembly gtest_main)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_sources(test_tcp_stream_manager PRIVATE test_tcp_stream_manager_allocation.cpp)
  target_link_options(test_tcp_stream_manager PRIVATE
    "-Wl,--wrap=posix_memalign" "-Wl,--wrap=free")
endif()
add_test(NAME test_tcp_stream_manager COMMAND test_tcp_stream_manager)
