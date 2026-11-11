cmake_minimum_required(VERSION 3.13)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(net_api_basic LANGUAGES CXX)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  include(CTest)
endif()

find_package(Threads REQUIRED)

foreach(module base memory buffer container concurrency ipc time io logging)
  add_library(basic_${module} INTERFACE)
  add_library(basic::${module} ALIAS basic_${module})
  target_include_directories(basic_${module} INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<INSTALL_INTERFACE:include>
  )
  target_compile_features(basic_${module} INTERFACE cxx_std_17)
endforeach()

target_link_libraries(basic_memory INTERFACE basic::base)
target_link_libraries(basic_container INTERFACE basic::base)
target_link_libraries(basic_concurrency INTERFACE basic::base Threads::Threads)
target_link_libraries(basic_buffer INTERFACE basic::base basic::concurrency)
target_link_libraries(basic_ipc INTERFACE basic::base Threads::Threads)
target_link_libraries(basic_time INTERFACE basic::base)
target_link_libraries(basic_io INTERFACE basic::base)

add_library(basic INTERFACE)
add_library(${PROJECT_NAME}::basic ALIAS basic)
if(NOT TARGET basic::basic)
  add_library(basic::basic ALIAS basic)
endif()
target_link_libraries(basic INTERFACE
  basic::base basic::memory basic::buffer basic::container basic::concurrency
  basic::ipc basic::time basic::io basic::logging
)

option(BASIC_BUILD_EXECUTION "Build the polling executor and its tests" OFF)
if(BUILD_TESTING)
  if(NOT TARGET gtest)
    find_package(GTest REQUIRED)
    add_library(gtest ALIAS GTest::gtest)
    add_library(gtest_main ALIAS GTest::gtest_main)
  endif()
  add_subdirectory(test)
  add_subdirectory(io/test)
  add_subdirectory(container)
endif()
add_subdirectory(config)
if(BASIC_BUILD_EXECUTION)
  add_subdirectory(execution)
endif()

add_executable(benchmark_mpmc_queue EXCLUDE_FROM_ALL
  concurrency/benchmark/mpmc_queue_benchmark.cpp
)
target_link_libraries(benchmark_mpmc_queue PRIVATE basic::concurrency)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(benchmark_mpmc_queue PRIVATE -O3)
endif()
