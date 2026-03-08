/**
 * @file config_parser.hpp
 * @author bfning
 * @brief Configuration file parser
 * @version 0.1
 * @date 2025-09-11
 */
#pragma once
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace detail {

class ConfigNode {
public:
    using ConfigConstIterator = std::unordered_map<std::string, ConfigNode>::const_iterator;
    using ConfigIterator = std::unordered_map<std::string, ConfigNode>::iterator;

    ConfigNode() = default;
    ~ConfigNode() = default;
    ConfigNode(const ConfigNode &other) = default;
    ConfigNode &operator=(const ConfigNode &other) = default;
    ConfigNode(ConfigNode &&other) noexcept = default;
    ConfigNode &operator=(ConfigNode &&other) noexcept = default;

    ConfigNode &operator[](const std::string &key) {
        return children_[key];
    }
    ConfigNode &operator=(const std::string &value) {
        value_ = value;
        return *this;
    }

    ConfigNode &operator=(const char *value) {
        value_ = value;
        return *this;
    }

    ConfigNode &operator=(int value) {
        value_ = std::to_string(value);
        return *this;
    }

    ConfigNode &operator=(double value) {
        value_ = std::to_string(value);
        return *this;
    }

    ConfigNode &operator=(bool value) {
        value_ = value ? "true" : "false";
        return *this;
    }

    ConfigNode &operator=(float value) {
        value_ = std::to_string(value);
        return *this;
    }

    ConfigNode &operator=(long value) {
        value_ = std::to_string(value);
        return *this;
    }
    ConfigNode &operator=(long long value) {
        value_ = std::to_string(value);
        return *this;
    }
    ConfigNode &operator=(unsigned value) {
        value_ = std::to_string(value);
        return *this;
    }
    ConfigNode &operator=(unsigned long value) {
        value_ = std::to_string(value);
        return *this;
    }
    ConfigNode &operator=(unsigned long long value) {
        value_ = std::to_string(value);
        return *this;
    }

    size_t erase(const std::string &key) {
        return children_.erase(key);
    }
    bool contains(const std::string &key) const {
        return children_.find(key) != children_.end();
    }
    bool empty() const {
        return children_.empty() && value_.empty();
    }
    size_t size() const {
        return children_.size();
    }
    void clear() {
        children_.clear();
        value_.clear();
    }

    ConfigNode &add(const std::string &key, const ConfigNode &node) {
        children_[key] = node;
        return children_[key];
    }

    template <typename T>
    T from_string() const {
        if constexpr (std::is_same_v<T, std::string>) {
            return value_;
        }
        else if constexpr (std::is_same_v<T, int>) {
            return std::stoi(value_);
        }
        else if constexpr (std::is_same_v<T, double>) {
            return std::stod(value_);
        }
        else if constexpr (std::is_same_v<T, bool>) {
            return value_ == "true" || value_ == "1" || value_ == "True" || value_ == "Yes" || value_ =
                       "yes" || value_ == "TRUE" || value_ == "YES";
        }
        else if constexpr (std::is_same_v<T, float>) {
            return std::stof(value_);
        }
        else if constexpr (std::is_same_v<T, long>) {
            return std::stol(value_);
        }
        else if constexpr (std::is_same_v<T, long long>) {
            return std::stoll(value_);
        }
        else if constexpr (std::is_same_v<T, unsigned>) {
            return static_cast<unsigned>(std::stoul(value_));
        }
        else if constexpr (std::is_same_v<T, unsigned long>) {
            return std::stoul(value_);
        }
        else if constexpr (std::is_same_v<T, unsigned long long>) {
            return std::stoull(value_);
        }
        else if constexpr (std::is_same_v<T, short>) {
            return std::stoi(value_);
        }
        else if constexpr (std::is_same_v<T, unsigned short>) {
            return std::stoi(value_);
        }
        else if constexpr (std::is_same_v<T, unsigned char>) {
            return static_cast<unsigned char>(std::stoi(value_));
        }
        else if constexpr (std::is_same_v<T, char>) {
            return value_[0];
        }

        return ::from_string<T>(value_);
    }

    template <>
    const char *from_string<const char *>() const {
        return value_.c_str();
    }

    template <typename T>
    inline T to() const {
        return from_string<T>();
    }

    std::vector<std::string> keys() const {
        std::vector<std::string> result;
        for (const auto &pair : children_) {
            result.push_back(pair.first);
        }
        return result;
    }

    ConfigConstIterator find(const std::string &key) const {
        return children_.find(key);
    }
    //
    ConfigConstIterator begin() const {
        return children_.begin();
    }

    ConfigConstIterator end() const {
        return children_.end();
    }

private:
    std::unordered_map<std::string, ConfigNode> children_;
    std::string value_;

private:
    // support list value
    std::multimap<std::string, ConfigNode> children_ordered_;
};

class IConfigParser {
public:
    IConfigParser() = default;
    ~IConfigParser() = default;

    bool load(const std::string &file_path);
    bool save(const std::string &file_path) const;
    bool save() const;

    std::string to_string() const;

    bool has(const std::string &key) const;
    bool empty() const {
        return root_.empty();
    }
    size_t size() const {
        return root_.size();
    }
    void clear() {
        root_.clear();
    }
    void erase(const std::string &key) {
        root_.erase(key);
    }

    ConfigNode &operator[](const std::string &key);
    const ConfigNode &operator[](const std::string &key) const;

    template <typename T>
    T get(const std::string &section, const std::string &key, const T &default_value) const {
        auto section_it = root_.find(section);
        if (section_it != root_.end()) {
            auto key_it = section_it->second.find(key);
            if (key_it != section_it->second.end()) {
                return from_string<T>(key_it->second.to_string());
            }
        }
        return default_value;
    }

    template <typename T>
    T set(const std::string &key, const T &value) {
        ConfigNode *node = &root_;
        size_t start = 0;
        while (true) {
            size_t dot_pos = key.find('.', start);
            std::string part = (dot_pos == std::string::npos) ? key.substr(start) : key.substr(start, dot_pos - start);
            node = &(*node)[part];
            if (dot_pos == std::string::npos) {
                break;
            }
            start = dot_pos + 1;
        }
        *node = value;
        return value;
    }

private:
    std::string file_path_;
    std::unordered_map<std::string, ConfigNode> root_;
};

class IniParser : public IConfigParser {
public:
    bool load(const char *file_path);
    std::string to_string();

protected:
    void parseLine(const std::string &line, const std::string &cur_section, int num);
};

class ConfParser : public IConfigParser {
public:
    bool load(const char *file_path);
    std::string to_string();

protected:
    void parseLine(const std::string &line, int num);
};

class XMLParser : public IConfigParser {
public:
    bool load(const char **file_path);
    std::string to_string();

protected:
    void parse();
};

}  // namespace detail
