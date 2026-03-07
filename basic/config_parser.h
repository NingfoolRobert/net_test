/**
 * @file config_parser.hpp
 * @author bfning
 * @brief Configuration file parser
 * @version 0.1
 * @date 2025-12-27
 */
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <sys/types.h>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>

namespace detail {
/**
 * @brief Configuration node that can hold different types of values
 *
 */
struct ConfigNode {
    using value_type = std::variant<std::string,
                                    bool,
                                    double,
                                    float,
                                    int32_t,
                                    uint32_t,
                                    int64_t,
                                    uint64_t,
                                    int16_t,
                                    uint16_t,
                                    char,
                                    int8_t,
                                    uint8_t,
                                    std::nullptr_t>;
    using object_type = std::unordered_map<std::string, ConfigNode>;
    using array_type = std::vector<ConfigNode>;

    std::variant<value_type, object_type, array_type> data;

public:
    explicit ConfigNode(int32_t v) : data(value_type(v)) {
    }
    explicit ConfigNode(double v) : data(value_type(v)) {
    }
    explicit ConfigNode(bool v) : data(value_type(v)) {
    }
    explicit ConfigNode(float v) : data(value_type(v)) {
    }
    explicit ConfigNode(uint16_t v) : data(value_type(v)) {
    }
    explicit ConfigNode(int16_t v) : data(value_type(v)) {
    }
    explicit ConfigNode(uint32_t v) : data(value_type(v)) {
    }
    explicit ConfigNode(uint64_t v) : data(value_type(v)) {
    }
    explicit ConfigNode(int64_t v) : data(value_type(v)) {
    }
    explicit ConfigNode(char v) : data(value_type(v)) {
    }
    explicit ConfigNode(uint8_t v) : data(value_type(v)) {
    }
    explicit ConfigNode(const char *v) : data(value_type(std::string(v))) {
    }
    explicit ConfigNode(const std::string &v) : data(value_type(v)) {
    }
    ConfigNode() = default;
    ~ConfigNode() = default;
    //
    ConfigNode(const ConfigNode &other) = default;
    ConfigNode &operator=(const ConfigNode &other) = default;
    ConfigNode(ConfigNode &&other) noexcept = default;
    ConfigNode &operator=(ConfigNode &&other) noexcept = default;
    /**
     * @brief Check if the node is an object
     * @return true if the node is an object, false otherwise
     */
    [[nodiscard]] bool is_object() const {
        return std::holds_alternative<object_type>(data);
    }
    /**
     * @brief Check if the node is an array
     * @return true if the node is an array, false otherwise
     */
    [[nodiscard]] bool is_array() const {
        return std::holds_alternative<array_type>(data);
    }
    /**
     * @brief Check if the node is a value
     * @return true if the node is a value, false otherwise
     */
    [[nodiscard]] bool is_value() const {
        return std::holds_alternative<value_type>(data);
    }
    /**
     * @brief Check if the node is empty
     * @return true if the node is empty, false otherwise
     */
    [[nodiscard]] bool empty() const {
        if (is_object()) {
            return std::get<object_type>(data).empty();
        }
        if (is_array()) {
            return std::get<array_type>(data).empty();
        }
        if (is_value()) {
            return std::get<value_type>(data).index() != std::variant_npos;
        }
        return true;
    }
    /**
     * @brief reset the ConfigNode to an empty state
     */
    void reset() {
        data = std::variant<value_type, object_type, array_type>{};
    }
    /**
     * @brief Clear the contents of the ConfigNode
     */
    void clear() {
        if (is_object()) {
            for (auto &[k, v] : std::get<object_type>(data)) {
                v.clear();
            }
            // Clear the object map
            std::get<object_type>(data).clear();
        }
        else if (is_array()) {
            for (auto &v : std::get<array_type>(data)) {
                v.clear();
            }
            // Clear the array vector
            std::get<array_type>(data).clear();
        }
        else if (is_value()) {
            data = value_type{};
        }
    }
    /**
     * @brief Check if the node is a string
     * @return true if the node is a string, false otherwise
     */
    [[nodiscard]] bool is_string() const {
        if (is_value()) {
            return std::holds_alternative<std::string>(std::get<value_type>(data));
        }
        return false;
    }
    /**
     * @brief Check if the node is an integer
     * @return true if the node is an integer, false otherwise
     */
    [[nodiscard]] bool is_int() const {
        if (is_value()) {
            return std::holds_alternative<int32_t>(std::get<value_type>(data)) ||
                   std::holds_alternative<int16_t>(std::get<value_type>(data)) ||
                   std::holds_alternative<uint8_t>(std::get<value_type>(data)) ||
                   std::holds_alternative<uint16_t>(std::get<value_type>(data)) ||
                   std::holds_alternative<uint32_t>(std::get<value_type>(data)) ||
                   std::holds_alternative<int64_t>(std::get<value_type>(data)) ||
                   std::holds_alternative<uint64_t>(std::get<value_type>(data));
        }
        return false;
    }
    /**
     * @brief Check if the node is a double
     * @return true if the node is a double, false otherwise
     */
    [[nodiscard]] bool is_double() const {
        if (is_value()) {
            return std::holds_alternative<double>(std::get<value_type>(data));
        }
        return false;
    }
    /**
     * @brief Check if the node is a boolean
     * @return true if the node is a boolean, false otherwise
     */
    [[nodiscard]] bool is_bool() const {
        if (is_value()) {
            return std::holds_alternative<bool>(std::get<value_type>(data));
        }
        return false;
    }
    /**
     * @brief Check if the node is a char
     */
    [[nodiscard]] bool is_char() const {
        if (is_value()) {
            return std::holds_alternative<char>(std::get<value_type>(data));
        }
        return false;
    }
    /**
     * @brief Get the ConfigNode for the specified key
     * @param key The key of the ConfigNode to get
     * @return The ConfigNode for the specified key
     */
    ConfigNode &operator[](const std::string &key) {
        if (!is_object()) {
            data = object_type{};
        }
        return std::get<object_type>(data)[key];
    }
    /**
     * @brief Get the ConfigNode for the specified key
     * @param key The key of the ConfigNode to get
     * @return The ConfigNode for the specified key, or a null ConfigNode if the key does not exist
     */
    const ConfigNode &operator[](const std::string &key) const {
        static ConfigNode null_node;
        if (!is_object()) {
            return null_node;
        }
        const auto &obj = std::get<object_type>(data);
        auto it = obj.find(key);
        if (it != obj.end()) {
            return it->second;
        }
        return null_node;
    }
    /**
     * @brief Get the ConfigNode at the specified index
     * @param index The index of the ConfigNode to get
     * @return The ConfigNode at the specified index
     */
    ConfigNode &operator[](size_t index) {
        if (!is_array()) {
            data = array_type{};
        }
        auto &arr = std::get<array_type>(data);
        if (index >= arr.size()) {
            arr.resize(index + 1);
        }
        return arr[index];
    }
    /**
     * @brief Get the ConfigNode at the specified index
     * @param index The index of the ConfigNode to get
     * @return The ConfigNode at the specified index, or a null ConfigNode if the index is out of bounds
     */
    const ConfigNode &operator[](size_t index) const {
        static ConfigNode null_node;
        if (!is_array()) {
            return null_node;
        }
        const auto &arr = std::get<array_type>(data);
        if (index >= arr.size()) {
            return null_node;
        }
        return arr[index];
    }
    /**
     * @brief Get the value stored in the ConfigNode
     * @tparam T The type of the value to get
     * @return The value stored in the ConfigNode, or a default-constructed T if the type does not match
     */
    template <typename T>
    T get(const T default_value = T{}) const {
        if (!std::holds_alternative<value_type>(data)) {
            return default_value;
        }
        auto is_all_of_digit = [](const std::string &str) {
            return std::all_of(str.begin(), str.end(), [](unsigned char c) { return std::isdigit(c); });
        };
        //
        const auto &val = std::get<value_type>(data);
        if (std::holds_alternative<T>(val)) {
            return std::get<T>(val);
        }
        //
        if (!std::holds_alternative<std::string>(val)) {
            return default_value;
        }

        // try to convert string to T
        const auto &str_val = std::get<std::string>(val);
        if constexpr (std::is_same_v<T, int>) {
            return std::stoi(str_val);
        }
        if constexpr (std::is_same_v<T, double>) {
            return std::stod(str_val);
        }
        else if constexpr (std::is_same_v<T, float>) {
            return std::stof(str_val);
        }
        else if constexpr (std::is_same_v<T, bool>) {

            //
            if (is_all_of_digit(str_val)) {
                return std::stoi(str_val) != 0;
            }
            if (str_val.find("0x") == 0 || str_val.find("0X") == 0) {
                return std::stoul(str_val, nullptr, 16) != 0;
            }
            if (str_val.find("0b") == 0 || str_val.find("0B") == 0) {
                return std::stoul(str_val, nullptr, 2) != 0;
            }
            //
            return str_val == "true" || str_val == "True" || str_val == "TRUE" || str_val == "t" || str_val == "T" ||
                   str_val == "yes" || str_val == "YES" || str_val == "Yes" || str_val == "y" || str_val == "Y" ||
                   str_val == "ON" || str_val == "on" || str_val == "On";
        }
        else if constexpr (std::is_same_v<T, int64_t>) {
            if (str_val.find("0x") == 0 || str_val.find("0X") == 0) {
                return std::stol(str_val, nullptr, 16);
            }
            if (str_val.find("0b") == 0 || str_val.find("0B") == 0) {
                return std::stol(str_val, nullptr, 2);
            }
            return std::stol(str_val);
        }
        else if constexpr (std::is_same_v<T, uint32_t>) {
            if (str_val.find("0x") == 0 || str_val.find("0X") == 0) {
                return static_cast<uint32_t>(std::stoul(str_val, nullptr, 16));
            }
            if (str_val.find("0b") == 0 || str_val.find("0B") == 0) {
                return static_cast<uint32_t>(std::stoul(str_val, nullptr, 2));
            }
            return static_cast<uint32_t>(std::stoul(str_val));
        }
        else if constexpr (std::is_same_v<T, uint64_t>) {
            if (str_val.find("0x") == 0 || str_val.find("0X") == 0) {
                return std::stoul(str_val, nullptr, 16);
            }
            if (str_val.find("0b") == 0 || str_val.find("0B") == 0) {
                return std::stoul(str_val, nullptr, 2);
            }
            return std::stoul(str_val);
        }
        else if constexpr (std::is_same_v<T, int16_t>) {
            if (str_val.find("0x") == 0 || str_val.find("0X") == 0) {
                return static_cast<int16_t>(std::stol(str_val, nullptr, 16));
            }
            if (str_val.find("0b") == 0 || str_val.find("0B") == 0) {
                return static_cast<int16_t>(std::stol(str_val, nullptr, 2));
            }
            return static_cast<int16_t>(std::stoi(str_val));
        }
        else if constexpr (std::is_same_v<T, uint16_t>) {
            if (str_val.find("0x") == 0 || str_val.find("0X") == 0) {
                return static_cast<uint16_t>(std::stoul(str_val, nullptr, 16));
            }
            if (str_val.find("0b") == 0 || str_val.find("0B") == 0) {
                return static_cast<uint16_t>(std::stoul(str_val, nullptr, 2));
            }
            return static_cast<uint16_t>(std::stoul(str_val));
        }
        else if constexpr (std::is_same_v<T, char>) {
            return str_val.empty() ? '\0' : str_val[0];
        }

        return default_value;
    }
    /**
     * @brief Set the value stored in the ConfigNode
     * @tparam T The type of the value to set
     * @param value The value to set
     */
    template <typename T>
    void set(const T &value) {
        data = value_type(value);
    }
    /**
     * @brief Append a value to the array
     * @tparam T The type of the value to append
     * @param value The value to append
     */
    template <typename T>
    void append(const T &value) {
        if (!is_array()) {
            data = array_type{};
        }
        auto &arr = std::get<array_type>(data);
        arr.emplace_back();
        arr.back().set(value);
    }
    /**
     * @brief Insert a key-value pair into the object
     * @tparam T The type of the value to insert
     * @param key The key of the value to insert
     * @param value The value to insert
     */
    template <typename T>
    void insert(std::string key, const T &value) {
        if (!is_object()) {
            data = object_type{};
        }
        auto &obj = std::get<object_type>(data);
        obj[key] = ConfigNode{};
        obj[key].set(value);
    }
    /**
     * @brief Set a key-value pair in the object
     */
    template <typename T>
    void set(std::string key, const T &value) {
        if (!is_object()) {
            data = object_type{};
        }
        auto &obj = std::get<object_type>(data);
        obj[key] = ConfigNode{};
        obj[key].set(value);
    }
    /**
     * @brief Set the value at the specified index in the array
     * @tparam T The type of the value to set
     * @param index The index of the value to set
     * @param value The value to set
     */
    template <typename T>
    void set(size_t index, const T &value) {
        if (!is_array()) {
            data = array_type{};
        }
        auto &arr = std::get<array_type>(data);
        if (index >= arr.size()) {
            arr.resize(index + 1);
        }
        arr[index].set(value);
    }
    /**
     * @brief Get the size of the array
     * @return The size of the array, or 0 if the node is not an array
     */
    [[nodiscard]] size_t array_size() const {
        if (!is_array()) {
            return 0;
        }
        return std::get<array_type>(data).size();
    }
    /**
     * @brief Get the size of the object
     * @return The size of the object, or 0 if the node is not an object
     */
    [[nodiscard]] size_t object_size() const {
        if (!is_object()) {
            return 0;
        }
        return std::get<object_type>(data).size();
    }
};

class ConfigDocument {
public:
    ConfigDocument() = default;
    ~ConfigDocument() = default;

    ConfigDocument(const ConfigDocument &other) = default;
    ConfigDocument &operator=(const ConfigDocument &other) = default;
    ConfigDocument(ConfigDocument &&other) noexcept = default;
    ConfigDocument &operator=(ConfigDocument &&other) noexcept = default;
    //
    ConfigNode &root() {
        return root_;
    }

    [[nodiscard]] const ConfigNode &root() const {
        return root_;
    }
    /**
     * @brief Load the configuration from a string
     * @tparam Parser The parser type to use
     * @param data The string data to load
     * @return True if the load was successful, false otherwise
     */
    template <typename Parser>
    bool parse(const std::string &data) {
        Parser parser;
        return parser.parse(data.c_str(), root_);
    }
    /// @brief Save the configuration to a string
    template <typename Parser>
    [[nodiscard]] std::string dump() const {
        Parser parser;
        return parser.dump(root_);
    }

private:
    ConfigNode root_;
};

class ConfigFile {
public:
    explicit ConfigFile(const char *file_path) : file_path_(file_path) {
    }
    ~ConfigFile() = default;

    ConfigFile(const ConfigFile &other) = default;
    ConfigFile &operator=(const ConfigFile &other) = default;
    ConfigFile(ConfigFile &&other) noexcept = default;
    ConfigFile &operator=(ConfigFile &&other) noexcept = default;

    [[nodiscard]] std::string data() const {
        std::ifstream file(file_path_, std::ios::in);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file for reading: " + file_path_);
        }
        //
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();
        return content;
    }

    void dump(const std::string &data) const {
        std::ofstream file(file_path_, std::ios::out | std::ios::trunc);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file for writing: " + file_path_);
        }
        //
        file << data;
        file.close();
    }

private:
    std::string file_path_;
};
// 配置验证
class ConfigValidator {
public:
    struct ValidationResult {
        bool valid;
        std::string message;
        std::vector<std::string> errors;
    };

    template <typename DomType>
    ValidationResult validate(const DomType &config, const DomType &schema) {
        ValidationResult result{true, "OK", {}};

        // 实现验证逻辑
        // 检查必需字段、类型匹配、值范围等
        // 如果发现错误，设置 result.valid = false 并添加错误信息到 result.errors
        return result;
    }
};

/**
 * @brief Base class for configuration file parsers
 *
 */
class ConfigParserOptions {
public:
    ConfigParserOptions() = default;
    ~ConfigParserOptions() = default;
    ConfigParserOptions(const ConfigParserOptions &other) = default;
    ConfigParserOptions &operator=(const ConfigParserOptions &other) = default;
    ConfigParserOptions(ConfigParserOptions &&other) noexcept = default;
    ConfigParserOptions &operator=(ConfigParserOptions &&other) noexcept = default;
    // 添加配置选项成员变量和方法
    /**
     * @brief Set an option
     * @tparam T The type of the option value
     * @param key The option key
     * @param value The option value
     *  {"fmt_boolean_true", "true(default):True:TRUE:T:t:1:0x1:0X1:yes:Yes:YES:Y:y:On:ON:on"}
     *  {"fmt_boolean_false", "false(default):False:FALSE:F:f:0:0x0:0X0:no:No:NO:N:n:Off:OFF:off"}
     *  {"fmt_double_precision", 6(default)}  // Default precision for floating-point numbers
     *  {"enter_line", "\n"(default),"\r\n"}  // Line ending format
     *  {"ini_key_value_delimiter", "="(default),":"}  // Key-value delimiter for INI files
     *  {"conf_key_value_delimiter", "="(default),":"}  // Key-value delimiter for CONF files
     */

    template <typename T>
    void set_option(const std::string &key, const T &value) {
        options_[key] = value;
    }

    template <typename T>
    T get_option(const char *key, const T default_value) const {
        auto it = options_.find(key);
        if (it != options_.end()) {
            return std::get<T>(it->second);
        }
        return default_value;
    }

private:
    std::unordered_map<std::string, std::variant<std::string, long int, bool, double, char>> options_;
};

class IConfigParser {
public:
    virtual ~IConfigParser() = default;  // Make destructor virtual
    virtual bool parse(const std::string &data, ConfigNode &root) = 0;
    virtual std::string dump(const ConfigNode &root) = 0;

    ConfigParserOptions &options() {
        return options_;
    }

    // Explicitly delete copy and move operations
    IConfigParser(const IConfigParser &) = delete;
    IConfigParser &operator=(const IConfigParser &) = delete;
    IConfigParser(IConfigParser &&) noexcept = delete;
    IConfigParser &operator=(IConfigParser &&) noexcept = delete;

private:
    ConfigParserOptions options_;
};

class IniParser : public IConfigParser {
public:
    virtual ~IniParser() = default;

    IniParser(const IniParser &) = delete;
    IniParser &operator=(const IniParser &) = delete;
    IniParser(IniParser &&) noexcept = delete;
    IniParser &operator=(IniParser &&) noexcept = delete;

    bool parse(const std::string &data, ConfigNode &root) override;
    std::string dump(const ConfigNode &root) override;
};

class ConfParser : public IConfigParser {
public:
    virtual ~ConfParser() = default;

    // Explicitly delete copy and move operations
    ConfParser(const ConfParser &) = delete;
    ConfParser &operator=(const ConfParser &) = delete;
    ConfParser(ConfParser &&) noexcept = delete;
    ConfParser &operator=(ConfParser &&) noexcept = delete;

    bool parse(const std::string &data, ConfigNode &root) override;
    std::string dump(const ConfigNode &root) override;
};

#ifdef CONFIG_PARSER_ENABLE_XML
class XMLParser : public IConfigParser {
public:
    XMLParser() = default;
    virtual ~XMLParser() = default;
    bool parse(const std::string &data, config::ConfigNode &root) override;
    std::string dump(const config::ConfigNode &root) override;
};
#endif  // CONFIG_PARSER_ENABLE_XML

#ifdef CONFIG_PARSER_ENABLE_TOML
class TOMLParser : public IConfigParser {
public:
    TOMLParser() = default;
    virtual ~TOMLParser() = default;
    bool parse(const std::string &data, config::ConfigNode &root) override;
    std::string dump(const config::ConfigNode &root) override;
};
#endif  // CONFIG_PARSER_ENABLE_TOML

#ifdef CONFIG_PARSER_ENABLE_JSON
class JSONParser : public IConfigParser {
public:
    JSONParser() = default;
    virtual ~JSONParser() = default;

    bool parse(const std::string &data, config::ConfigNode &root) = 0;
    std::string dump(const config::ConfigNode &root) = 0;
};
#endif  // CONFIG_PARSER_ENABLE_JSON

#ifdef CONFIG_PARSER_ENABLE_YAML
class YAMLParser : public IConfigParser {
public:
    YAMLParser() = default;
    virtual ~YAMLParser() = default;
    bool parse(const std::string &data, config::ConfigNode &root) override;
    std::string dump(const config::ConfigNode &root) override;
};
#endif  // CONFIG_PARSER_ENABLE_YAML
}  // namespace detail
