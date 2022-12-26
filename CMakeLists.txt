cmake_minimum_required(VERSION 3.10)

project(net_api_test)

set(CMAKE_CXX_FLAGS_DEBUG  "$ENV{CMAKE_CXX_FLAGS} -g -ggdb -O0 -Wall")
set(CMAKE_CXX_FLAGS_RELEASE "$ENV{CMAKE_CXX_FLAGS} -O2 -W -DNDEBUG")
set(TOP_DIR  ${CMAKE_CURRENT_SOURCE_DIR})

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
	message(STATUS "INSTALL_SYS_DEFAULT_DIR: 1 ${CMAKE_INSTALL_PREFIX}")	
	set(CMAKE_INSTALL_PREFIX ${TOP_DIR}/install CACHE PATH "default install prefix" FORCE)
endif()

message(STATUS "INSTALL_DIR: ${CMAKE_INSTALL_PREFIX}")

message(STATUS "TOP_DIR: ${TOP_DIR}")

set(CMAKE_DEBUG_POSTFIX "_d")

add_subdirectory(core)
add_subdirectory(example)
add_subdirectory(test)

