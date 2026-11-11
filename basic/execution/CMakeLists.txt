cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(net_test_execution LANGUAGES CXX)
  include(CTest)
endif()

find_package(Threads REQUIRED)
add_library(polling_thread_executor polling_thread_executor.cpp)
add_library(basic::execution ALIAS polling_thread_executor)
target_compile_features(polling_thread_executor PUBLIC cxx_std_17)
target_include_directories(polling_thread_executor PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>)
target_link_libraries(polling_thread_executor PUBLIC Threads::Threads)

if(BUILD_TESTING)
  if(NOT TARGET gtest_main)
    find_package(GTest REQUIRED)
    add_library(gtest_main ALIAS GTest::gtest_main)
  endif()
  add_executable(test_sync_pair test/test_sync_pair.cpp)
  target_compile_features(test_sync_pair PRIVATE cxx_std_17)
  target_link_libraries(test_sync_pair PRIVATE gtest_main Threads::Threads)
  add_test(NAME test_sync_pair COMMAND test_sync_pair)
  set_tests_properties(test_sync_pair PROPERTIES TIMEOUT 30)

  add_executable(test_polling_thread_executor test/test_polling_thread_executor.cpp)
  target_link_libraries(test_polling_thread_executor PRIVATE
    polling_thread_executor gtest_main)
  target_link_options(test_polling_thread_executor PRIVATE
    "-Wl,--wrap=pthread_setaffinity_np"
    "-Wl,--wrap=_Znwm"
    "-Wl,--wrap=nanosleep")
  add_test(NAME test_polling_thread_executor COMMAND test_polling_thread_executor)
  set_tests_properties(test_polling_thread_executor PROPERTIES TIMEOUT 60)
  if(NOT TARGET test_basic_time_wheel)
    add_executable(test_basic_time_wheel test/test_time_wheel.cpp)
    target_link_libraries(test_basic_time_wheel PRIVATE gtest_main polling_thread_executor)
    add_test(NAME test_basic_time_wheel COMMAND test_basic_time_wheel)
  endif()
endif()
