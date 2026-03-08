#include "../config_parser.h"
#include "gtest/gtest.h"
#include <cstddef>
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
    EXPECT_EQ(root["key3"].is_int(), true);
    EXPECT_EQ(root["key3"].get<int>(0), 123);
    
    // doc.parse<ConfParser>(conf_file.data());
    // EXPECT_EQ(doc.root().is_object(), true);
    // EXPECT_EQ(doc.root().as<ConfigNode::object_type>().size(), 4);
    // EXPECT_EQ(doc.root().as<ConfigNode::object_type>().at("key1").as<std::string>(), "value1");
    // EXPECT_STREQ(doc.root().as<ConfigNode::object_type>().at("key2").as<std::string>(), "value with spaces");
    // EXPECT_EQ(doc.root().as<ConfigNode::object_type>().at("key3").as<int>(), 123);
    // EXPECT_EQ(doc.root().as<ConfigNode::object_type>().at("key4").as<bool>(), true);
}