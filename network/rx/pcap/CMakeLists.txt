cmake_minimum_required(VERSION 3.13)

find_library(PCAP_LIBRARY NAMES pcap)
if(NOT PCAP_LIBRARY)
  message(FATAL_ERROR "libpcap is required to build pcap_rx")
endif()

add_library(pcap_rx STATIC rx_pcap_mcbc.cpp rx_pcap_live_mcbc.cpp
  rx_pcap_tcp.cpp ip_reassembly.cpp ip_fragment_assembly.cpp)
target_include_directories(pcap_rx
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_link_libraries(pcap_rx PUBLIC ${PCAP_LIBRARY} tcp_reassembly)

add_subdirectory(test)
