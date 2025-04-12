cmake_minimum_required(VERSION 3.13)

#find_library(fmt REQUIRED )

add_executable(test_flog  test_flog.cpp)
target_link_libraries(test_flog PRIVATE flogging)
