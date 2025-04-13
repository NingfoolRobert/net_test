cmake_minimum_required(VERSION 3.13)

set(TARGET_NAME flogging)

add_library(${TARGET_NAME}  INTERFACE)
add_library(${PROJECT_NAME}::${TARGET_NAME} ALIAS ${TARGET_NAME})

target_include_directories(${TARGET_NAME}  INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<INSTALL_INTERFACE:include>
  )

#
target_link_libraries(${TARGET_NAME} INTERFACE basic)
if(USE_SYS_FLOG)
  target_link_libraries(${TARGET_NAME} INTERFACE fmt::fmt)
endif()

add_subdirectory(test)
add_subdirectory(thirdparty)

