#include "../config_parser.h"
#include "gtest/gtest.h"
#include <cstddef>
#include <cstdio>
#include <gtest/gtest.h>
#include <sstream>

class ConfigParserTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Code here will be called immediately after the constructor (right
        conf_data = R"(
# This is a comment
key1=value1
key2 = "value with spaces"
key3=123
key4 = true
)";

        std::ofstream out(conf_file_path, std::ios::out | std::ios::trunc);
        out << conf_data;
        out.close();
    }
    void TearDown() override {
        // Code here will be called immediately after each test (right
    }

    std::string conf_data;
    std::string conf_file_path = "./test_config.conf";
};

TEST_F(ConfigParserTest, ConfParser) {
    using namespace detail;

    ConfigFile conf_file(conf_file_path.c_str());
    ConfigDocument doc;
    doc.parse<ConfParser>(conf_file.data());
    auto& root = doc.root();
    EXPECT_EQ(root["key1"].is_string(), true);
    EXPECT_EQ(root["key1"].get<std::string>(""), "value1");
    EXPECT_EQ(root["key2"].is_string(), true);
    EXPECT_EQ(root["key2"].get<std::string>(""), "value with spaces");
    EXPECT_EQ(root["key3"].is_value(), true);
    //EXPECT_EQ(root["key3"].is_int(), true);
    EXPECT_EQ(root["key3"].get<int>(0), 123);
    EXPECT_EQ(root["key4"].is_value(), true);
    //EXPECT_EQ(root["key4"].is_bool(), true);
    EXPECT_EQ(root["key4"].get<bool>(false), true);
}

TEST_F(ConfigParserTest, ReadsCurrentContentsFromEachFile) {
    const std::string second_path = conf_file_path + ".second";
    struct Cleanup {
        const std::string& path;
        ~Cleanup() { std::remove(path.c_str()); }
    } cleanup{second_path};
    detail::ConfigFile first(conf_file_path.c_str()), second(second_path.c_str());
    first.dump("alpha");
    second.dump("beta");
    const auto first_read = first.data();
    EXPECT_EQ(first_read, "alpha");
    EXPECT_EQ(second.data(), "beta");
    first.dump(std::string(4097, 'x'));
    EXPECT_EQ(first.data(), std::string(4097, 'x'));
    EXPECT_EQ(first_read, "alpha");
    EXPECT_EQ(second.data(), "beta");
}
