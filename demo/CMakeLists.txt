
set(TARGET_NAME test_demo)

add_definitions(-DSYS_LOG)

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/version.h.in"
	"${CMAKE_CURRENT_BINARY_DIR}/version.h")

file(GLOB ${TARGET_NAME}_HEADER_FILES 
	LIST_DIRECTORIES  FALSE  CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/*.h*"
	)
file(GLOB ${TARGET_NAME}_SOURCE_FILES
	LIST_DIRECTORIES FALSE CONFIGURE_DEPENDS 
	"${CMAKE_CURRENT_SOURCE_DIR}/*.c*"
	)
	

include_directories(${CMAKE_CURRENT_BINARY_DIR})
add_executable(${TARGET_NAME} ${${TARGET_NAME}_HEADER_FILES} ${${TARGET_NAME}_SOURCE_FILES})
if(UNIX) 
target_link_options(${TARGET_NAME}  PRIVATE -static)
endif()
target_link_libraries(${TARGET_NAME} PRIVATE  core)
target_link_libraries(${TARGET_NAME} PRIVATE  cmdline)


#设置属性
if(UNIX) 
set_target_properties(${TARGET_NAME} PROPERTIES 
	COMPILE_FLAGS	"-pthread"
	LINK_FLAGS "-pthread")
endif()
#符号导出
set_target_properties(${TARGET_NAME}	PROPERTIES 
		WINDOWS_EXPORT_ALL_SYMBOLS TRUE)
