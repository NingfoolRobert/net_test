cmake_minimum_required(VERSION 3.13)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(net_api_basic LANGUAGES CXX)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  include(CTest)
endif()

set(TARGET_NAME basic)

add_library(${TARGET_NAME} INTERFACE)
add_library(${PROJECT_NAME}::${TARGET_NAME} ALIAS ${TARGET_NAME})

target_include_directories(${TARGET_NAME} INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<INSTALL_INTERFACE:include>
)

target_compile_features(${TARGET_NAME} INTERFACE cxx_std_17)
find_package(Threads REQUIRED)
target_link_libraries(${TARGET_NAME} INTERFACE Threads::Threads)
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

add_executable(benchmark_mpmc_queue EXCLUDE_FROM_ALL
  benchmark/mpmc_queue_benchmark.cpp
)
target_link_libraries(benchmark_mpmc_queue PRIVATE basic Threads::Threads)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(benchmark_mpmc_queue PRIVATE -O3)
endif()
