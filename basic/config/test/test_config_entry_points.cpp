#include "../config_parser.h"

#include <gtest/gtest.h>

TEST(ConfigEntryPoints, ConfEntryPointParsesAndSerializes) {
  detail::ConfParser parser;
  detail::ConfigNode root;
  ASSERT_TRUE(parser.parse("answer=42\nname=example\n", root));
  EXPECT_EQ(root["answer"].get<int>(), 42);
  EXPECT_EQ(root["name"].get<std::string>(), "example");
  detail::ConfigNode restored;
  ASSERT_TRUE(parser.parse(parser.dump(root), restored));
  EXPECT_EQ(restored["answer"].get<int>(), 42);
  EXPECT_EQ(restored["name"].get<std::string>(), "example");
}

TEST(ConfigEntryPoints, IniEntryPointParsesAndSerializes) {
  detail::IniParser parser;
  detail::ConfigNode root;
  ASSERT_TRUE(parser.parse("[service]\nenabled=true\nport=8080\n", root));
  EXPECT_TRUE(root["service"]["enabled"].get<bool>());
  EXPECT_EQ(root["service"]["port"].get<int>(), 8080);
  detail::ConfigNode restored;
  ASSERT_TRUE(parser.parse(parser.dump(root), restored));
  EXPECT_TRUE(restored["service"]["enabled"].get<bool>());
  EXPECT_EQ(restored["service"]["port"].get<int>(), 8080);
}
