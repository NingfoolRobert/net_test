cmake_minimum_required(VERSION 3.13)

set(TARGET_NAME config)

add_library(${TARGET_NAME} STATIC)
add_library(${PROJECT_NAME}::${TARGET_NAME} ALIAS ${TARGET_NAME})

target_include_directories(${TARGET_NAME} INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<INSTALL_INTERFACE:include>
)

target_sources(${TARGET_NAME}
  PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/config_parser.cpp")

add_subdirectory(test)
