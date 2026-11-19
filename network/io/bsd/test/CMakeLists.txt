
cmake_minimum_required(VERSION 3.13)


set(TARGET_NAME  test_bsd_mcbc)

add_executable(${TARGET_NAME}  test_bsd_mcbc.cpp)
target_link_libraries(${TARGET_NAME} PRIVATE 
  bsd
   )

