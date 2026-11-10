#include "../config_parser.h"
#include <gtest/gtest.h>
#include <limits>

namespace {
detail::ConfigNode tree(bool with_null = true) {
    detail::ConfigNode root;
    root["title"].set(std::string("中文 & <tag> \"quoted\"\nline"));
    root["enabled"].set(true);
    root["port"].set(int32_t(8001));
    root["ratio"].set(1.25);
    root["nested"]["name"].set(std::string("value"));
    root["items"][std::size_t(0)].set(int32_t(7));
    root["items"][std::size_t(1)].set(std::string("true"));
    root["empty_object"].data = detail::ConfigNode::object_type{};
    root["empty_array"].data = detail::ConfigNode::array_type{};
    if (with_null) root["nothing"].set(nullptr);
    return root;
}
void expect_tree(const detail::ConfigNode &root, bool with_null = true) {
    EXPECT_EQ(root["title"].get<std::string>(), "中文 & <tag> \"quoted\"\nline");
    EXPECT_TRUE(root["enabled"].get<bool>());
    EXPECT_EQ(root["port"].get<int>(), 8001);
    EXPECT_DOUBLE_EQ(root["ratio"].get<double>(), 1.25);
    EXPECT_EQ(root["nested"]["name"].get<std::string>(), "value");
    EXPECT_EQ(root["items"].array_size(), 2U);
    EXPECT_EQ(root["items"][std::size_t(0)].get<int>(), 7);
    EXPECT_EQ(root["items"][std::size_t(1)].get<std::string>(), "true");
    EXPECT_TRUE(root["empty_object"].is_object());
    EXPECT_EQ(root["empty_object"].object_size(), 0U);
    EXPECT_TRUE(root["empty_array"].is_array());
    EXPECT_EQ(root["empty_array"].array_size(), 0U);
    if (with_null) {
        ASSERT_TRUE(root["nothing"].is_value());
        EXPECT_TRUE(std::holds_alternative<std::nullptr_t>(std::get<detail::ConfigNode::value_type>(root["nothing"].data)));
    }
}
template <typename Parser>
void roundtrip(bool with_null = true) {
    Parser parser;
    detail::ConfigNode result;
    ASSERT_TRUE(parser.parse(parser.dump(tree(with_null)), result));
    expect_tree(result, with_null);
}
template <typename Parser>
void rejected(const std::string &input) {
    Parser parser;
    detail::ConfigNode root("unchanged");
    EXPECT_FALSE(parser.parse(input, root));
    EXPECT_EQ(root.get<std::string>(), "unchanged");
}
}  // namespace
#ifdef CONFIG_PARSER_ENABLE_JSON
TEST(JsonConfig, RoundtripsNestedTypedValues) { roundtrip<detail::JSONParser>(); }
TEST(JsonConfig, RejectsInvalidInputAndPreservesRoot) {
    rejected<detail::JSONParser>("{bad}");
    rejected<detail::JSONParser>("{} trailing");
    rejected<detail::JSONParser>(std::string(150, '[') + "0" + std::string(150, ']'));
}
TEST(JsonConfig, HandlesIntegerLimitsNullAndTopLevelArray) {
    detail::JSONParser parser;
    detail::ConfigNode root;
    ASSERT_TRUE(parser.parse("[18446744073709551615,-9223372036854775808,null,42]", root));
    EXPECT_EQ(root[std::size_t(0)].get<uint64_t>(), std::numeric_limits<uint64_t>::max());
    EXPECT_EQ(root[std::size_t(1)].get<int64_t>(), std::numeric_limits<int64_t>::min());
    EXPECT_EQ(root[std::size_t(3)].get<uint64_t>(), 42U);
    EXPECT_EQ(root[std::size_t(0)].get<int>(-1), -1);
    detail::ConfigNode infinite(std::numeric_limits<double>::infinity());
    EXPECT_THROW(parser.dump(infinite), std::invalid_argument);
}
#endif
#ifdef CONFIG_PARSER_ENABLE_TOML
TEST(TomlConfig, RoundtripsNestedValues) { roundtrip<detail::TOMLParser>(false); }
TEST(TomlConfig, ParsesTablesArraysEscapesAndDates) {
    detail::TOMLParser parser;
    detail::ConfigNode root;
    ASSERT_TRUE(parser.parse(R"(
name = "demo"
number = 0x10
when = 2026-10-14
[server]
port = 8001
[[workers]]
name = "one"
[[workers]]
name = "two"
)", root));
    EXPECT_EQ(root["number"].get<int>(), 16);
    EXPECT_EQ(root["when"].get<std::string>(), "2026-10-14");
    EXPECT_EQ(root["server"]["port"].get<int>(), 8001);
    EXPECT_EQ(root["workers"].array_size(), 2U);
    EXPECT_EQ(root["workers"][std::size_t(1)]["name"].get<std::string>(), "two");
}
TEST(TomlConfig, RejectsInvalidOrUnrepresentableValues) {
    rejected<detail::TOMLParser>("a = [");
    rejected<detail::TOMLParser>("a=1\na=2");
    detail::TOMLParser parser;
    EXPECT_THROW(parser.dump(tree()), std::invalid_argument);
    detail::ConfigNode scalar(42);
    EXPECT_THROW(parser.dump(scalar), std::invalid_argument);
    scalar["too_large"].set(std::numeric_limits<uint64_t>::max());
    EXPECT_THROW(parser.dump(scalar), std::invalid_argument);
}
#endif
#ifdef CONFIG_PARSER_ENABLE_YAML
TEST(YamlConfig, RoundtripsNestedTypedValues) { roundtrip<detail::YAMLParser>(); }
TEST(YamlConfig, HandlesScalarTypesQuotesAnchorsAndBlockStrings) {
    detail::YAMLParser parser;
    detail::ConfigNode root;
    ASSERT_TRUE(parser.parse(R"(
base: &base
  port: 8001
copy: *base
quoted: "true"
explicit: !!str 42
hex: 0x10
octal: 0o20
boolean: true
nothing: null
multiline: |
  hello
  world
)", root));
    EXPECT_EQ(root["copy"]["port"].get<int>(), 8001);
    EXPECT_EQ(root["quoted"].get<std::string>(), "true");
    EXPECT_EQ(root["explicit"].get<std::string>(), "42");
    EXPECT_EQ(root["hex"].get<int>(), 16);
    EXPECT_EQ(root["octal"].get<int>(), 16);
    EXPECT_TRUE(root["boolean"].is_bool());
    EXPECT_EQ(root["multiline"].get<std::string>(), "hello\nworld\n");
}
TEST(YamlConfig, RejectsInvalidDuplicateRecursiveAndMultipleDocuments) {
    rejected<detail::YAMLParser>("a: [");
    rejected<detail::YAMLParser>("a: 1\na: 2");
    rejected<detail::YAMLParser>("a: &a [*a]");
    rejected<detail::YAMLParser>("a: *missing");
    rejected<detail::YAMLParser>("---\na: 1\n---\nb: 2");
    rejected<detail::YAMLParser>("a: !!bool invalid");
    rejected<detail::YAMLParser>("a: .nan");
}
#endif
#ifdef CONFIG_PARSER_ENABLE_XML
TEST(XmlConfig, RoundtripsNestedTypedValues) { roundtrip<detail::XMLParser>(); }
TEST(XmlConfig, ReadsOrdinaryXmlAttributesRepeatedElementsAndEntities) {
    detail::XMLParser parser;
    detail::ConfigNode root;
    ASSERT_TRUE(parser.parse("<settings version='1'><port>8001</port><item>a</item><item>b &amp; c</item></settings>", root));
    EXPECT_EQ(root["@version"].get<std::string>(), "1");
    EXPECT_EQ(root["port"].get<int>(), 8001);
    EXPECT_EQ(root["item"].array_size(), 2U);
    EXPECT_EQ(root["item"][std::size_t(1)].get<std::string>(), "b & c");
}
TEST(XmlConfig, RejectsMalformedDtdInvalidTypedValuesAndInvalidCharacters) {
    rejected<detail::XMLParser>("<settings>");
    rejected<detail::XMLParser>("<!DOCTYPE root [<!ENTITY x 'secret'>]><root>&x;</root>");
    rejected<detail::XMLParser>("<config format='basic-config' type='boolean'>wrong</config>");
    rejected<detail::XMLParser>("<config format='basic-config' type='object'><entry type='string'>no key</entry></config>");
    detail::XMLParser parser;
    detail::ConfigNode invalid(std::string("a\0b", 3));
    EXPECT_THROW(parser.dump(invalid), std::invalid_argument);
    invalid.set(std::string(1, '\1'));
    EXPECT_THROW(parser.dump(invalid), std::invalid_argument);
}
#endif
