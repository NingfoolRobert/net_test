add_executable(test_basic_io test_rx.cpp test_error_code.cpp test_file_util.cpp)
target_link_libraries(test_basic_io PRIVATE basic::io gtest_main Threads::Threads)
if(UNIX)
  target_sources(test_basic_io PRIVATE test_tcp.cpp test_backends.cpp
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../network/io/bsd/rtx_bsd_tcp.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../network/io/bsd/rx_bsd_mcbc.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../network/io/bsd/tx_bsd_mcbc.cpp")
  target_include_directories(test_basic_io PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../network"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../network/common")
endif()
add_test(NAME test_basic_io COMMAND test_basic_io)
