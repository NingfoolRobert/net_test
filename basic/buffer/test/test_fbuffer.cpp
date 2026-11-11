#include "../fbuffer.h"
#include <gtest/gtest.h>
#include <iostream>
#include <sstream>

TEST(fbuffer, fbuffer_append) {
    detail::fbuffer<256> buf;

    buf.append("HelloWorld", 11);
    EXPECT_EQ(buf.capacity(), 256);
    EXPECT_EQ(buf.length(), 11);
}

TEST(fbuffer, fbuffer_ostream) {

    detail::fbuffer<128> buf;
    buf.append("helloworld", 11);
    std::stringstream ss;
    ss << buf;
    printf("%s\n", ss.str().c_str());
}
