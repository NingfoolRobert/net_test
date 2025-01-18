
set(TARGET_NAME  basic) 

add_library(${TARGET_NAME}  INTERFACE)

target_include_directories(${TARGET_NAME}  INTERFACE 
  "<BUILD_INTERFACE>::${CMAKE_CURRENT_SOURCE_DIR}/*.h"
  "<BUILD_INTERFACE>::${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
  )

add_subdirectory(test)
