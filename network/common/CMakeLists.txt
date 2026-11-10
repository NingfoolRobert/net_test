cmake_minimum_required(VERSION 3.13)

set(TARGET_NAME  net_common)

add_library(${TARGET_NAME} INTERFACE)
add_library(${PROJECT_NAME}::${TARGET_NAME} ALIAS ${TARGET_NAME})

target_include_directories(${TARGET_NAME} 
  INTERFACE 
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  )

add_library(tcp_reassembly STATIC tcp_stream_reassembly.cpp
  tcp_four_tuple_filter.cpp tcp_stream_manager.cpp)
target_link_libraries(tcp_reassembly PUBLIC net_common)
target_compile_features(tcp_reassembly PUBLIC cxx_std_17)
target_include_directories(tcp_reassembly SYSTEM PRIVATE
  ${PROJECT_SOURCE_DIR}/basic/container)

add_executable(benchmark_tcp_stream_manager EXCLUDE_FROM_ALL
  benchmark/tcp_stream_manager_benchmark.cpp)
target_link_libraries(benchmark_tcp_stream_manager PRIVATE tcp_reassembly)

add_subdirectory(test)
