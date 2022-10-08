cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 11)
set(TARGET_NAME test_log)

file(GLOB  ${TARGET_NAME}_SOURCE_FILES 
	LIST_DIRECTORIES  FALSE  CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/test_log_module.cpp")


add_executable(${TARGET_NAME} ${${TARGET_NAME}_SOURCE_FILES})
target_link_libraries(${TARGET_NAME} core)
#set_target_properties(${TARGET_NAME} PROPERTIES  LINKER_LANGUAGE Fortran)
set_target_properties(${TARGET_NAME} PROPERTIES 
	COMPILE_FLAGS	"-pthread"
	LINK_FLAGS		"-pthread")
