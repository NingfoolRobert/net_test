cmake_minimum_required(VERSION 3.13)

add_executable(bsd_mcbc_demo bsd_mcbc_demo.cpp)
add_executable(bsd_tcp_demo bsd_tcp_demo.cpp)
add_executable(bsd_domain_demo bsd_domain_demo.cpp)

foreach(target bsd_mcbc_demo bsd_tcp_demo bsd_domain_demo)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
  target_link_libraries(${target} PRIVATE net_common)
endforeach()
