# Cross-module regressions stay here; unit tests live with their modules.
file(GLOB SRC_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
add_executable(test_basic ${SRC_FILES})
target_link_libraries(test_basic PRIVATE gtest_main basic)
add_test(NAME test_basic COMMAND test_basic)

foreach(module base buffer container concurrency ipc time logging)
  file(GLOB MODULE_TESTS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/../${module}/test/*.cpp")
  # SwissTable keeps its existing independently buildable test target.
  list(FILTER MODULE_TESTS EXCLUDE REGEX "/test_swiss_table\\.cpp$")
  add_executable(test_basic_${module} ${MODULE_TESTS})
  target_link_libraries(test_basic_${module} PRIVATE gtest_main basic::${module})
  add_test(NAME test_basic_${module} COMMAND test_basic_${module})
endforeach()

# The time wheel remains testable without building the optional executor.
add_executable(test_basic_time_wheel ../execution/test/test_time_wheel.cpp)
target_link_libraries(test_basic_time_wheel PRIVATE gtest_main basic::time Threads::Threads)
add_test(NAME test_basic_time_wheel COMMAND test_basic_time_wheel)

