cmake_minimum_required(VERSION 3.10)
set(TARGET_NAME DemoNetApi)

if(MSVC)
	add_definitions(-D_EXPORTING)
	set(${TARGET_NAME}_RC_FILE  "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.rc")
	configure_file("${CMAKE_CURRENT_SOURCE_DIR}/${TARGET_NAME}.rc.in"
		"${${TARGET_NAME}_RC_FILE}")
endif()


configure_file("${CMAKE_CURRENT_SOURCE_DIR}/version.h.in"
	"${CMAKE_CURRENT_BINARY_DIR}/version.h")

file(GLOB ${TARGET_NAME}_HEADER_FILES 
	LIST_DIRECTORIES  FALSE  CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/*.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
	)
file(GLOB ${TARGET_NAME}_SOURCE_FILES
	LIST_DIRECTORIES FALSE CONFIGURE_DEPENDS 
	"${CMAKE_CURRENT_SOURCE_DIR}/*.c*")

#add_library(${TARGET_NAME}_static 
#	STATIC
#	${${TARGET_NAME}_HEADER_FILES}
#	${${TARGET_NAME}_SOURCE_FILES}
#	)
#target_link_libraries(${TARGET_NAME}_static PRIVATE  core)
## 想要生成动态库和静态库相同名称的的库文件，则需要一个指令set_target_properties
## 这里只是通过重新命名的方法使其名字相同的，构建时还需不能相同
#set_target_properties(${TARGET_NAME}_static PROPERTIES OUTPUT_NAME "${TARGET_NAME}")
## 获取相关属性也是通过set_target_properties
#get_target_property(OUTPUT_VALUE ${TARGET_NAME}_static OUTPUT_NAME)
include_directories(${CMAKE_CURRENT_BINARY_DIR})
add_library(${TARGET_NAME} 
	SHARED
	${${TARGET_NAME}_HEADER_FILES}
	${${TARGET_NAME}_SOURCE_FILES}
	${${TARGET_NAME}_RC_FILE}
	)
if(MVSC)
	#set_target_properties(${TARGET_NAME} PROPERTIES "${${TARGET_NAME}_RC_FILE}")
endif()
## 动态库版本号设置 , VERSION指代动态库版本，SOVERSION 指代 API 版本。
set_target_properties(${TARGET_NAME} PROPERTIES VERSION 0.1 SOVERSION 1)
# 其中，lib***.so.1.2为动态库的文件名(realname)，lib***.so.1为动态库的别名(soname)，
# lib***.so为动态库的链接名（linkname）。
target_link_libraries(${TARGET_NAME} PRIVATE  core)

set_target_properties(${TARGET_NAME}  PROPERTIES  CXX_VISIBILITY_PRESET	  hidden)
set_target_properties(${TARGET_NAME}  PROPERTIES  C_VISIBILITY_PRESET	  hidden)
set_target_properties(${TARGET_NAME}  PROPERTIES  VISIBILITY_INLINES_HIDDEN ON)

file(GLOB ${TARGET_NAME}_PUBLIC_HEADER_FILES  
	"${CMAKE_CURRENT_SOURCE_DIR}/BizUser.h"
	"${TOP_DIR}/core/Common.h"
	)

set_target_properties(${TARGET_NAME} PROPERTIES PUBLIC_HEADER "${${TARGET_NAME}_PUBLIC_HEADER_FILES}")
message(STATUS "PUBLIC_HEADER: ${PUBLIC_HEADER}")
#符号导出
set_target_properties(${TARGET_NAME}	PROPERTIES 
	WINDOWS_EXPORT_ALL_SYMBOLS TRUE)
#install 
install(TARGETS  ${TARGET_NAME} 
	#${TARGET_NAME}_static
	EXPORT  ${TARGET_NAME}_targets 
	PUBLIC_HEADER		DESTINATION		include 
	ARCHIVE				DESTINATION		lib 
	LIBRARY				DESTINATION		lib 
	RUNTIME				DESTINATION		bin)

install(EXPORT  ${TARGET_NAME}_targets  
	NAMESPACE	${TARGET_NAME}::
	FILE		${TARGET_NAME}Config.cmake 
	DESTINATION lib/cmake/${TARGET_NAME}
	)

#add_subdirectory(test)
