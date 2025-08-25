#include "../fstring.h"
#include <assert.h>
#include <gtest/gtest.h>
#include <iostream>

TEST(fstring, fstring_length_test) {
    detail::fstring<128> str("HelloWorld");
    EXPECT_EQ(str.length(), 10);
    EXPECT_EQ(detail::fstring<128>::npos, -1);
}

TEST(fstring, fstring_append_test) {
    detail::fstring<128> str;
    str.append("HelloWorld", 11);
    EXPECT_EQ(str.str(), "HelloWorld");
    EXPECT_EQ(str.length(), 10);
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

TEST(fstring, fstringContructWithString) {
    using namespace detail;
    std::string str("hello world.");
    fstring<128> tf(str);
    EXPECT_STREQ(tf.c_str(), str.c_str());
    EXPECT_EQ(tf.length(), str.length());
    EXPECT_EQ(tf.size(), str.size());

    fstring<8> tf1(str);
    std::string ret = tf1.c_str();
    // printf("%s\n", tf1.c_str());
    EXPECT_EQ(tf1.capacity(), 8);
    EXPECT_STREQ(tf1.c_str(), "hello w");

    fstring<128> tf2(tf);
    EXPECT_EQ(tf.capacity(), tf2.capacity());
    EXPECT_TRUE(tf == tf2);

    fstring<128> tf3('A', 64);
    EXPECT_EQ(tf3[0], 'A');
    for (auto i = 0; i < 64; ++i) {
        EXPECT_EQ(tf3[i], 'A');
    }

    std::string_view sv(str.c_str());
    fstring<128> tf4(sv);
    EXPECT_STREQ(tf4.c_str(), str.c_str());
    EXPECT_EQ(tf4.capacity(), 128);
    EXPECT_EQ(tf4.length(), str.length());
}

TEST(fstring, fstringAppend) {
    using namespace detail;
    fstring<16> str("Hello,World!");
    str.append("by", 2);
    EXPECT_EQ(str.length(), 14);
    str.append("test", 4);
    EXPECT_EQ(str.length(), 14);
    str.append("t", 1);
    EXPECT_EQ(str.length(), str.capacity() - 1);
}

TEST(fstring, fstringComp){
  using namespace detail;
  fstring<16> str("hello,world");
  fstring<16> str1("hello,World");
  EXPECT_TRUE(str1 > str);
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
