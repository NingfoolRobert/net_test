cmake_minimum_required(VERSION 3.13)

set(TARGET_NAME  basic)

add_library(${TARGET_NAME}  INTERFACE)
add_library(${PROJECT_NAME}::${TARGET_NAME} ALIAS ${TARGET_NAME})

target_include_directories(${TARGET_NAME}  INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<INSTALL_INTERFACE:include>
  )

add_subdirectory(test)
