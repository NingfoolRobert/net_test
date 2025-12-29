#include "config_parser.h"
#include <algorithm>
#include <cstdint>
#include <sstream>

namespace detail {

namespace util {

inline std::string trim(std::string str) {
    const char *whitespace = " \t\n\r\f\v";
    str.erase(0, str.find_first_not_of(whitespace));
    str.erase(str.find_last_not_of(whitespace) + 1);
    return str;
}

inline std::string to_lower(const std::string &str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return std::tolower(c); });
    return result;
}

inline std::string to_upper(const std::string &str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return std::toupper(c); });
    return result;
}

inline std::string escape(const std::string &str) {
    std::string result;
    result.reserve(str.size());
    for (char c : str) {
        switch (c) {
            case '\n':
                result += "\\n";
                break;
            case '\t':
                result += "\\t";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\\':
                result += "\\\\";
                break;
            case '"':
                result += "\\\"";
                break;
            case '\'':
                result += "\\'";
                break;
            default:
                result += c;
                break;
        }
    }
    return result;
}

// Escape special characters in a string
inline std::string unescape(const std::string &str) {
    std::string result;
    result.reserve(str.size());
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '\\' && i + 1 < str.size()) {
            switch (str[i + 1]) {
                case 'n':
                    result += '\n';
                    ++i;
                    break;
                case 't':
                    result += '\t';
                    ++i;
                    break;
                case 'r':
                    result += '\r';
                    ++i;
                    break;
                case '\\':
                    result += '\\';
                    ++i;
                    break;
                case '"':
                    result += '"';
                    ++i;
                    break;
                case '\'':
                    result += '\'';
                    ++i;
                    break;
                default:
                    result += str[i];
                    break;
            }
        }
        else {
            result += str[i];
        }
    }
    return result;
}
//
inline std::string add_quotes_if_needed(const std::string &str) {
    bool needs_quotes = false;
    for (char c : str) {
        if (std::isspace(static_cast<unsigned char>(c)) || || c == '"' || c == '\'' || c == '\\' || c == '\r' ||
            c == '\n' || c == '\t' || c == '\t') {
            needs_quotes = true;
            break;
        }
    }
    // Add quotes if needed
    if (needs_quotes) {
        return "\"" + escape(str) + "\"";
    }
    return str;
}
// Trim surrounding quotes from a string
inline std::string trim_quotes(const std::string &str) {
    if (str.size() >= 2) {
        if ((str.front() == '"' && str.back() == '"') || (str.front() == '\'' && str.back() == '\'')) {
            return str.substr(1, str.size() - 2);
        }
    }
    return str;
}

static inline std::string config_node_value_to_string(const ConfigNode &node, const ConfigParserOptions &options) {
    if (!node.is_value()) {
        return "";
    }
    //
    const auto &val = std::get<ConfigNode::value_type>(node.data);
    if (std::holds_alternative<std::string>(val)) {
        return std::get<std::string>(val);
    }
    if (std::holds_alternative<int32_t>(val)) {
        return std::to_string(std::get<int32_t>(val));
    }
    if (std::holds_alternative<uint32_t>(val)) {
        return std::to_string(std::get<uint32_t>(val));
    }
    if (std::holds_alternative<int64_t>(val)) {
        return std::to_string(std::get<int64_t>(val));
    }
    if (std::holds_alternative<uint64_t>(val)) {
        return std::to_string(std::get<uint64_t>(val));
    }
    if (std::holds_alternative<int16_t>(val)) {
        return std::to_string(std::get<int16_t>(val));
    }
    if (std::holds_alternative<uint16_t>(val)) {
        return std::to_string(std::get<uint16_t>(val));
    }
    if (std::holds_alternative<uint8_t>(val)) {
        return std::to_string(std::get<uint8_t>(val));
    }
    if (std::holds_alternative<float>(val)) {
        return std::to_string(std::get<float>(val));
    }
    if (std::holds_alternative<double>(val)) {
        char tmp[64] = {0};
        sprintf(tmp, "%.*lf", options.get_option<int>("fmt_double_precision", 6), std::get<double>(val));
        return std::string(tmp);
    }
    if (std::holds_alternative<bool>(val)) {
        std::string true_str = options.get_option<std::string>("fmt_boolean_true", "true");
        std::string false_str = options.get_option<std::string>("fmt_boolean_false", "false");
        return std::get<bool>(val) ? true_str : false_str;
    }
    if (std::holds_alternative<char>(val)) {
        return std::string(1, std::get<char>(val));
    }
    return "";
}

}  // namespace util

//

bool IniParser::parse(const std::string &data, ConfigNode &root) {
    std::istringstream stream(data);
    std::string line;
    std::string current_section;
    char delimiter = options().get_option<std::string>("ini_key_value_delimiter", "=")[0];
    while (std::getline(stream, line)) {
        line = util::trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') {
            continue;  // Skip empty lines and comments
        }
        if (line.front() == '[' && line.back() == ']') {
            current_section = util::trim(line.substr(1, line.size() - 2));
            root[current_section] = ConfigNode::object_type{};
            continue;
        }
        //
        if (current_section.empty() || line.find(delimiter) == std::string::npos) {
            continue;  // Skip lines without '=' if no section is defined
        }
        //
        auto delimiter_pos = line.find(delimiter);
        if (delimiter_pos == std::string::npos) {
            continue;  // Skip lines without '='
        }
        //
        std::string key = util::trim(line.substr(0, delimiter_pos));
        std::string value = util::trim_quotes(util::unescape(line.substr(delimiter_pos + 1)));
        root[current_section][key] = ConfigNode(value);
    }
    //
    return true;
}
//
std::string IniParser::dump(const ConfigNode &root) {
    static std::string enter_line = options().get_option("enter_line", "\n");
    static std::string key_value_delimiter = options().get_option("ini_key_value_delimiter", "=");
    if (root.is_object()) {
        std::string result;
        for (const auto &[section, section_node] : std::get<ConfigNode::object_type>(root.data)) {
            result += "[" + section + "]" + enter_line;
            for (const auto &[key, value] : std::get<ConfigNode::object_type>(section_node.data)) {
                if (value.is_object() || value.is_array()) {
                    continue;
                }
                // Recursively dump nested objects and arrays
                result.append(key).append(" ").append(key_value_delimiter).append(" ").append(util::add_quotes_if_needed(dump(value))).append(enter_line);
            }
            result += enter_line;
        }
        return result;
    }
    //
    if (root.is_value()) {
        return util::config_node_value_to_string(root, options());
    }
    return "";
}

// Config Parser Implementation
bool ConfParser::parse(const std::string &data, ConfigNode &root) {
    std::istringstream stream(data);
    std::string line;
    char delimiter = options().get_option<std::string>("conf_key_value_delimiter", "=")[0];
    while (std::getline(stream, line)) {
        line = util::trim(line);
        if (line.empty() || line[0] == '#') {
            continue;  // Skip empty lines and comments
        }
        auto delimiter_pos = line.find(delimiter);
        if (delimiter_pos == std::string::npos) {
            continue;  // Skip lines without '='
        }
        //
        std::string key = util::trim(line.substr(0, delimiter_pos));
        std::string value = util::trim_quotes(util::unescape(line.substr(delimiter_pos + 1)));
        root[key] = ConfigNode(value);
    }
    return true;
}
//
std::string ConfParser::dump(const ConfigNode &root) {
    static std::string enter_line = options().get_option("enter_line", "\n");
    static std::string key_value_delimiter = options().get_option("conf_key_value_delimiter", "=");
    if (root.is_object()) {
        std::string result;
        for (const auto &[key, value] : std::get<ConfigNode::object_type>(root.data)) {
            if (value.is_object() || value.is_array()) {
                continue;
            }
            // Recursively dump nested objects and arrays
            result += key + " " + key_value_delimiter + " " + dump(value) + enter_line;
        }
        return result;
    }
    else if (root.is_value()) {
        return util::config_node_value_to_string(root, options());
    }
    //
    if (root.is_array()) {
        throw std::runtime_error("IConfigParser::dump does not support array type");
    }
    return "";
}
}  // namespace detail