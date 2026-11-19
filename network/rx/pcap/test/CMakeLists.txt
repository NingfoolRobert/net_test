cmake_minimum_required(VERSION 3.13)

set(TARGET_NAME test_ip_reassembler)

add_executable(${TARGET_NAME} test_ip_reassembler.cpp)
target_link_libraries(${TARGET_NAME} PRIVATE pcap_rx gtest gtest_main)
add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME})

add_executable(test_rx_pcap_tcp test_rx_pcap_tcp.cpp)
target_link_libraries(test_rx_pcap_tcp PRIVATE pcap_rx gtest_main)
add_test(NAME test_rx_pcap_tcp COMMAND test_rx_pcap_tcp)
