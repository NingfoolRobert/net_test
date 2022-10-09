
cmake_minimum_required(VERSION 3.10)

project(test_cmake  VERSION  1.0.0)

if(MSVC)
	set(${PROJECT_NAME}_RC_FILE  "${CMAKE_BINARY_DIR}/VersionInfo.rc")
	configure_file("${CMAKE_CURRENT_SOURCE_DIR}/VersionInfo.rc.in"
		"${${PROJECT_NAME}_RC_FILE}")

	set(CMAKE_C_FLAGS_DEBUG  "${CMAKE_C_FLAGS_DEBUG} /MTd")
	set(CMAKE_CXX_FLAGS_DEBUG  "${CMAKE_CXX_FLAGS_DEBUG} /MTd")
	set(CMAKE_C_FLAGS_RELEASE  "${CMAKE_C_FLAGS_RELEASE} /MT")
	set(CMAKE_CXX_FLAGS_RELEASE  "${CMAKE_CXX_FLAGS_RELEASE} /MT")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4100 /wd4505 /wd4996")
endif()

add_executable(${PROJECT_NAME}  main.cpp ${${PROJECT_NAME}_RC_FILE})
