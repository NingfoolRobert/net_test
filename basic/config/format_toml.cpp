#include "format_util.h"
#include "../thirdparty/tomlplusplus/toml.hpp"
#include <sstream>

namespace detail {
namespace {
formats::Json read_toml(const toml::node &node, std::size_t depth = 0) {
    formats::check_depth(depth);
    if (const auto *table = node.as_table()) {
        auto result = formats::Json::object();
        for (const auto &[key, value] : *table) result[std::string(key.str())] = read_toml(value, depth + 1);
        return result;
    }
    if (const auto *array = node.as_array()) {
        auto result = formats::Json::array();
        for (const auto &value : *array) result.push_back(read_toml(value, depth + 1));
        return result;
    }
    return node.visit([](const auto &value) -> formats::Json {
        using V = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<V, toml::table> || std::is_same_v<V, toml::array>) {
            throw std::runtime_error("Expected TOML scalar");
        } else {
        using T = typename std::decay_t<decltype(value)>::value_type;
        if constexpr (std::is_same_v<T, toml::date> || std::is_same_v<T, toml::time> ||
                      std::is_same_v<T, toml::date_time>) {
            std::ostringstream out;
            out << value.get();
            return out.str();  // ConfigNode has no temporal value type.
        } else if constexpr (std::is_arithmetic_v<T> || std::is_same_v<T, std::string>) {
            if constexpr (std::is_floating_point_v<T>)
                if (!std::isfinite(value.get())) throw std::runtime_error("Non-finite TOML value");
            return value.get();
        } else throw std::runtime_error("Unsupported TOML value");
        }
    });
}
template <typename Container, typename... Key>
void insert_toml(Container &container, const formats::Json &value, Key &&...key) {
    if (value.is_object()) {
        toml::table table;
        for (auto it = value.begin(); it != value.end(); ++it) insert_toml(table, it.value(), it.key());
        if constexpr (std::is_same_v<Container, toml::array>) container.push_back(std::move(table));
        else container.insert(std::forward<Key>(key)..., std::move(table));
    } else if (value.is_array()) {
        toml::array array;
        for (const auto &item : value) insert_toml(array, item);
        if constexpr (std::is_same_v<Container, toml::array>) container.push_back(std::move(array));
        else container.insert(std::forward<Key>(key)..., std::move(array));
    } else {
        auto insert = [&](auto scalar) {
            if constexpr (std::is_same_v<Container, toml::array>) container.push_back(std::move(scalar));
            else container.insert(std::forward<Key>(key)..., std::move(scalar));
        };
        if (value.is_string()) insert(value.get<std::string>());
        else if (value.is_boolean()) insert(value.get<bool>());
        else if (value.is_number_float()) insert(value.get<double>());
        else if (value.is_number_integer()) {
            if (value.is_number_unsigned() && value.get<uint64_t>() > static_cast<uint64_t>(INT64_MAX))
                throw std::invalid_argument("TOML integers must fit in int64_t");
            insert(value.get<int64_t>());
        } else throw std::invalid_argument("TOML cannot represent null");
    }
}
}  // namespace
bool TOMLParser::parse(const std::string &data, ConfigNode &root) {
    try {
        auto parsed = formats::from_json(read_toml(toml::parse(data)));
        root = std::move(parsed);
        return true;
    } catch (const toml::parse_error &) { return false; }
      catch (const std::runtime_error &) { return false; }
}
std::string TOMLParser::dump(const ConfigNode &root) {
    const auto value = formats::to_json(root);
    if (!value.is_object()) throw std::invalid_argument("TOML root must be an object");
    toml::table table;
    for (auto it = value.begin(); it != value.end(); ++it) insert_toml(table, it.value(), it.key());
    std::ostringstream out;
    out << table;
    return out.str();
}
}  // namespace detail
