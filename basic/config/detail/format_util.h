#pragma once
#include "../config_parser.h"
#include <climits>
#include <nlohmann/json.hpp>

namespace detail::formats {
using Json = nlohmann::json;
inline void check_depth(std::size_t depth) {
    if (depth > 128) throw std::runtime_error("Configuration nesting exceeds 128 levels");
}
inline ConfigNode from_json(const Json &value, std::size_t depth = 0) {
    check_depth(depth);
    ConfigNode node;
    if (value.is_object()) {
        node.data = ConfigNode::object_type{};
        for (auto it = value.begin(); it != value.end(); ++it)
            node[it.key()] = from_json(it.value(), depth + 1);
    } else if (value.is_array()) {
        node.data = ConfigNode::array_type{};
        for (const auto &item : value)
            std::get<ConfigNode::array_type>(node.data).push_back(from_json(item, depth + 1));
    } else if (value.is_string()) node.set(value.get<std::string>());
    else if (value.is_boolean()) node.set(value.get<bool>());
    else if (value.is_number_unsigned()) {
        const auto n = value.get<uint64_t>();
        if (n <= static_cast<uint64_t>(INT32_MAX)) node.set(static_cast<int32_t>(n));
        else if (n <= static_cast<uint64_t>(INT64_MAX)) node.set(static_cast<int64_t>(n));
        else node.set(n);
    } else if (value.is_number_integer()) {
        const auto n = value.get<int64_t>();
        if (n >= INT32_MIN && n <= INT32_MAX) node.set(static_cast<int32_t>(n));
        else node.set(n);
    } else if (value.is_number_float()) node.set(value.get<double>());
    else node.set(nullptr);
    return node;
}
inline Json to_json(const ConfigNode &node, std::size_t depth = 0) {
    check_depth(depth);
    if (node.is_object()) {
        Json result = Json::object();
        for (const auto &entry : std::get<ConfigNode::object_type>(node.data))
            result[entry.first] = to_json(entry.second, depth + 1);
        return result;
    }
    if (node.is_array()) {
        Json result = Json::array();
        for (const auto &item : std::get<ConfigNode::array_type>(node.data))
            result.push_back(to_json(item, depth + 1));
        return result;
    }
    return std::visit([](const auto &value) -> Json {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, char>) return std::string(1, value);
        else {
            if constexpr (std::is_floating_point_v<T>)
                if (!std::isfinite(value)) throw std::invalid_argument("Non-finite configuration number");
            return value;
        }
    }, std::get<ConfigNode::value_type>(node.data));
}
}  // namespace detail::formats
