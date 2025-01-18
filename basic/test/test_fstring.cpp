#include "../fstring.h"
#include <assert.h>
#include <gtest/gtest.h>
#include <iostream>

TEST(fstring, fstring_length_test) {
    detail::fstring<128> str("HelloWorld");
    EXPECT_EQ(str.length(), 10);
    EXPECT_EQ(detail::fstring<128>::npos, -1);
}

TEST(fstring, fstring_append_test){
  detail::fstring<128> str;
  str.append("HelloWorld", 11);
  EXPECT_EQ(str.str(),  "HelloWorld");
  EXPECT_EQ(str.length(),  10);
}

TEST(fstring, fstring_operator_test) {
  using namespace detail;
  fstring<128> str("HelloWorld");
  EXPECT_EQ(str[0], 'H');
}

TEST(fstring, fstring_memeory_test) {
  using namespace detail; 
  fstring<128> str;
  EXPECT_EQ(sizeof(str), 128);
}

// struct person_t  {
//   detail::fstring<16>     name;
//   int       age;
// };
//
// int main() {
//     detail::fstring<1024> data;
//
//     data.append("testdata", 10);
//
//     std::cout << data.data() << std::endl;
//
//     std::cout << data.length() << std::endl;
//
//     std::cout << (data == "testdata") << std::endl;
//
//     assert(sizeof(person_t) == 20);
//     assert(sizeof(data) == 1024);
//
//     std::cout << data.substr(4, 3) << std::endl;
//
//     std::cout << data.at(3) << std::endl;
//     data[4] = 'm';
//     std::cout << data.c_str() << std::endl;
//
//     std::cout << data << std::endl;
//
//     char ch = data[1024];
//     return 0;
// }
