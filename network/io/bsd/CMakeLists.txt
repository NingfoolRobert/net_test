cmake_minimum_required(VERSION 3.13)


set(TARGET_NAME bsd)

add_library(${TARGET_NAME} STATIC  rx_bsd_mcbc.cpp  tx_bsd_mcbc.cpp)

target_include_directories(${TARGET_NAME} 
  INTERFACE 
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  )

#target_sources(${TARGET_NAME} 
#  INTERFACE 
#  "${CMAKE_CURRENT_SOURCE_DIR}/rx_bsd_mcbc.cpp"
#  )

target_link_libraries(${TARGET_NAME} PUBLIC basic)
target_link_libraries(${TARGET_NAME} PUBLIC net_common)


add_subdirectory(test)
