add_executable(test_swiss_table test/test_swiss_table.cpp)
target_link_libraries(test_swiss_table PRIVATE basic::container gtest_main)
add_test(NAME test_swiss_table COMMAND test_swiss_table)
