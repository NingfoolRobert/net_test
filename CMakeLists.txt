cmake_minimum_required(VERSION 3.10)

project(net_api_test 
		VERSION  0.1.0)

message(STATUS "Platform : ${CMAKE_HOST_SYSTEM}")
if(CMAKE_CL_64)
	message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} x64")
else()
	message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} x86")
endif(CMAKE_CL_64)
message(STATUS "Compiler Version: ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Build Mode: ${CMAKE_BUILD_TYPE}")
if(MSVC)
	add_definitions(-D_UNICODE)
	SET(CMAKE_C_FLAGS_DEBUG  "${CMAKE_C_FLAGS_DEBUG} /MTd")
	SET(CMAKE_CXX_FLAGS_DEBUG  "${CMAKE_CXX_FLAGS_DEBUG} /MTd")
	SET(CMAKE_C_FLAGS_RELEASE  "${CMAKE_C_FLAGS_RELEASE} /MT")
	SET(CMAKE_CXX_FLAGS_RELEASE  "${CMAKE_CXX_FLAGS_RELEASE} /MT")
	SET(CMAKE_CXX_FLAGS_RELEASE  "${CMAKE_CXX_FLAGS} /wd4100 /wd4505 /wd4996")
	#Build Type
	if(CMAKE_BUILD_TYPE STREQUAL "Debug")
		add_compile_options(
			/Zi
			/Od
		)
		add_link_options(/Debug)
		add_definitions(-W3)
	else()
		add_definitions(-W0)
	endif()
else()
	#
	set(CMAKE_CXX_FLAGS_DEBUG  "$ENV{CMAKE_CXX_FLAGS} -g -ggdb -O0 -Wall")
	set(CMAKE_CXX_FLAGS_RELEASE "$ENV{CMAKE_CXX_FLAGS} -O3 -W -DNDEBUG -lm")
endif()
set(TOP_DIR  ${CMAKE_CURRENT_SOURCE_DIR})

message(STATUS "VERSION: ${PROJECT_VERSION}")
math(EXPR PROJECT_VERSION_VALUE "((${PROJECT_VERSION_MAJOR} << 16) + (${PROJECT_VERSION_MINOR} << 8)) + ${PROJECT_VERSION_PATCH}")
# OUTPUT_FORMAT DECIMAL)
message(STATUS "PROJECT_VERSION_VALUE: ${PROJECT_VERSION_VALUE}")
string(TIMESTAMP PROJECT_COMPILE_TIME %Y%m%d%H%M%S)
message(STATUS "COMPILE_TIME: ${PROJECT_COMPILE_TIME}")

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
add_subdirectory(third_party)
add_subdirectory(demo)
add_subdirectory(basic)


enable_testing()

