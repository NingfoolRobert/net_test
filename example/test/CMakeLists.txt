cmake_minimum_required(VERSION 3.10)

set(TARGET_NAME TestDemo)

file(GLOB ${TARGET_NAME}_HEADER_FILES 
	LIST_DIRECTORIES FALSE CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/*.h*"
	)

file(GLOB ${TARGET_NAME}_SOURCE_FILES
	LIST_DIRECTORIES FALSE CONFIGURE_DEPENDS 
	"${CMAKE_CURRENT_SOURCE_DIR}/*.c*")

find_package(DemoNetApi 
	PATHS  ${CMAKE_INSTALL_PREFIX})

add_executable(${TARGET_NAME} ${${TARGET_NAME}_SOURCE_FILES} ${${TARGET_NAME}_HEADER_FILES})
target_link_libraries(${TARGET_NAME} ${TOP_DIR}/install/lib/libDemoNetApi_d.so)
target_include_directories(${TARGET_NAME}
	 PUBLIC 
	 ${TOP_DIR}/install/include
	 )


set_target_properties(${TARGET_NAME} PROPERTIES 
	COMPILE_FLAGS	"-pthread"
	LINK_FLAGS		"-pthread")
