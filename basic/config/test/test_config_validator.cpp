#include "../config_parser.h"
#include <gtest/gtest.h>

TEST(ConfigValidator, ChecksRequiredTypesBoundsNestedPropertiesAndItems) {
    detail::ConfigNode schema;
    schema["type"].set(std::string("object"));
    schema["required"].append(std::string("server"));
    schema["additionalProperties"].set(false);
    auto &server = schema["properties"]["server"];
    server["type"].set(std::string("object"));
    server["required"].append(std::string("port"));
    server["properties"]["port"]["type"].set(std::string("integer"));
    server["properties"]["port"]["minimum"].set(1);
    server["properties"]["port"]["maximum"].set(65535);
    auto &workers = schema["properties"]["workers"];
    workers["type"].set(std::string("array"));
    workers["minItems"].set(1);
    workers["items"]["type"].set(std::string("string"));
    detail::ConfigNode config;
    config["server"]["port"].set(8001);
    config["workers"].append(std::string("worker"));
    detail::ConfigValidator validator;
    EXPECT_TRUE(validator.validate(config, schema).valid);
    config["server"]["port"].set(70000);
    config["workers"][std::size_t(0)].set(42);
    config["extra"].set(true);
    auto result = validator.validate(config, schema);
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(result.errors.size(), 3U);
    config.data = detail::ConfigNode::object_type{};
    result = validator.validate(config, schema);
    EXPECT_FALSE(result.valid);
    EXPECT_NE(result.errors.front().find("missing required field: server"), std::string::npos);
}
TEST(ConfigValidator, RejectsUnknownAndMalformedSchemaRules) {
    detail::ConfigValidator validator;
    detail::ConfigNode value(1), schema;
    schema.data = detail::ConfigNode::object_type{};
    schema["unsupported"].set(true);
    EXPECT_FALSE(validator.validate(value, schema).valid);
    schema.data = detail::ConfigNode::object_type{};
    schema["type"].set(std::string("unknown"));
    EXPECT_FALSE(validator.validate(value, schema).valid);
    schema.data = detail::ConfigNode::object_type{};
    schema["minimum"].set(std::string("bad"));
    EXPECT_FALSE(validator.validate(value, schema).valid);
    schema.data = detail::ConfigNode::object_type{};
    schema["type"].set(std::string("integer"));
    detail::ConfigNode boolean(true);
    EXPECT_FALSE(validator.validate(boolean, schema).valid);
}
