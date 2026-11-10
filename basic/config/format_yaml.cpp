#include "format_util.h"
#include <yaml.h>
#include <exception>
#include <regex>
#include <unordered_map>

namespace detail {
namespace {
using formats::Json;
Json yaml_scalar(const std::string &text, const std::string &tag, bool implicit) {
    if (tag == YAML_STR_TAG || (!implicit && tag.empty())) return text;
    const bool resolve = implicit || !tag.empty();
    if (!resolve) return text;
    if (text.empty() || text == "~" || text == "null" || text == "Null" || text == "NULL") {
        if (implicit || tag == YAML_NULL_TAG) return nullptr;
    }
    if (text == "true" || text == "True" || text == "TRUE") {
        if (implicit || tag == YAML_BOOL_TAG) return true;
    }
    if (text == "false" || text == "False" || text == "FALSE") {
        if (implicit || tag == YAML_BOOL_TAG) return false;
    }
    static const std::regex integer(R"([+-]?(?:[0-9]+|0x[0-9a-fA-F]+|0o[0-7]+))");
    static const std::regex nonfinite(R"([+-]?\.(?:inf|Inf|INF|nan|NaN|NAN))");
    if ((implicit || tag == YAML_FLOAT_TAG) && std::regex_match(text, nonfinite))
        throw std::runtime_error("Non-finite YAML number");
    static const std::regex floating(R"([+-]?(?:[0-9]+\.[0-9]*|\.[0-9]+|[0-9]+)(?:[eE][+-]?[0-9]+)?)");
    if ((implicit || tag == YAML_INT_TAG) && std::regex_match(text, integer)) {
        std::string number = text;
        const bool negative = number[0] == '-';
        if (number[0] == '+' || number[0] == '-') number.erase(0, 1);
        int base = 10;
        if (number.rfind("0x", 0) == 0) { base = 16; number.erase(0, 2); }
        else if (number.rfind("0o", 0) == 0) { base = 8; number.erase(0, 2); }
        auto magnitude = std::stoull(number, nullptr, base);
        if (negative) {
            const auto limit = static_cast<uint64_t>(INT64_MAX) + 1;
            if (magnitude > limit) throw std::runtime_error("YAML integer out of range");
            return magnitude == limit ? INT64_MIN : -static_cast<int64_t>(magnitude);
        }
        return magnitude;
    }
    if ((implicit || tag == YAML_FLOAT_TAG) && std::regex_match(text, floating)) {
        const auto value = std::stod(text);
        if (!std::isfinite(value)) throw std::runtime_error("Non-finite YAML number");
        return value;
    }
    if (!tag.empty()) throw std::runtime_error("Unsupported or invalid YAML scalar tag");
    return text;
}
class YamlReader {
public:
    explicit YamlReader(const std::string &data) {
        if (!yaml_parser_initialize(&parser_)) throw std::bad_alloc();
        yaml_parser_set_input_string(&parser_, reinterpret_cast<const unsigned char *>(data.data()), data.size());
    }
    ~YamlReader() {
        if (has_event_) yaml_event_delete(&event_);
        yaml_parser_delete(&parser_);
    }
    void next() {
        if (has_event_) yaml_event_delete(&event_);
        has_event_ = false;
        if (!yaml_parser_parse(&parser_, &event_)) throw std::runtime_error("Invalid YAML");
        has_event_ = true;
        if (++events_ > 1000000) throw std::runtime_error("YAML event limit exceeded");
    }
    void expect(yaml_event_type_t type) {
        if (event_.type != type) throw std::runtime_error("Unexpected YAML event");
        next();
    }
    Json document() {
        next();
        expect(YAML_STREAM_START_EVENT);
        if (event_.type == YAML_STREAM_END_EVENT) return nullptr;
        expect(YAML_DOCUMENT_START_EVENT);
        auto value = read(0);
        expect(YAML_DOCUMENT_END_EVENT);
        if (event_.type != YAML_STREAM_END_EVENT) throw std::runtime_error("Only one YAML document is supported");
        return value;
    }
private:
    static std::string text(const yaml_char_t *s) {
        return s ? reinterpret_cast<const char *>(s) : "";
    }
    Json read(std::size_t depth) {
        formats::check_depth(depth);
        Json value;
        std::string anchor;
        if (event_.type == YAML_ALIAS_EVENT) {
            auto it = anchors_.find(text(event_.data.alias.anchor));
            if (it == anchors_.end()) throw std::runtime_error("Unknown or recursive YAML alias");
            expanded_bytes_ += it->second.dump().size();
            if (expanded_bytes_ > 64 * 1024 * 1024) throw std::runtime_error("YAML alias expansion limit exceeded");
            value = it->second;
            next();
        } else if (event_.type == YAML_SCALAR_EVENT) {
            anchor = text(event_.data.scalar.anchor);
            value = yaml_scalar(std::string(reinterpret_cast<const char *>(event_.data.scalar.value),
                                            event_.data.scalar.length),
                                text(event_.data.scalar.tag), event_.data.scalar.plain_implicit != 0);
            next();
        } else if (event_.type == YAML_SEQUENCE_START_EVENT) {
            anchor = text(event_.data.sequence_start.anchor);
            auto tag = text(event_.data.sequence_start.tag);
            if (!tag.empty() && tag != YAML_SEQ_TAG) throw std::runtime_error("Unsupported YAML sequence tag");
            value = Json::array();
            next();
            while (event_.type != YAML_SEQUENCE_END_EVENT) value.push_back(read(depth + 1));
            next();
        } else if (event_.type == YAML_MAPPING_START_EVENT) {
            anchor = text(event_.data.mapping_start.anchor);
            auto tag = text(event_.data.mapping_start.tag);
            if (!tag.empty() && tag != YAML_MAP_TAG) throw std::runtime_error("Unsupported YAML mapping tag");
            value = Json::object();
            next();
            while (event_.type != YAML_MAPPING_END_EVENT) {
                if (event_.type != YAML_SCALAR_EVENT) throw std::runtime_error("YAML keys must be scalar strings");
                std::string key(reinterpret_cast<const char *>(event_.data.scalar.value), event_.data.scalar.length);
                if (value.contains(key)) throw std::runtime_error("Duplicate YAML key");
                next();
                value[key] = read(depth + 1);
            }
            next();
        } else throw std::runtime_error("Expected YAML value");
        if (!anchor.empty()) anchors_[anchor] = value;
        return value;
    }
    yaml_parser_t parser_{};
    yaml_event_t event_{};
    bool has_event_ = false;
    std::size_t events_ = 0, expanded_bytes_ = 0;
    std::unordered_map<std::string, Json> anchors_;
};
struct YamlDocument {
    yaml_document_t value{};
    bool owned = true;
    YamlDocument() {
        if (!yaml_document_initialize(&value, nullptr, nullptr, nullptr, 1, 1)) throw std::bad_alloc();
    }
    ~YamlDocument() { if (owned) yaml_document_delete(&value); }
    int scalar(const std::string &text, const char *tag, yaml_scalar_style_t style) {
        if (text.size() > static_cast<std::size_t>(INT_MAX)) throw std::length_error("YAML scalar too long");
        int id = yaml_document_add_scalar(&value, reinterpret_cast<const yaml_char_t *>(tag),
            reinterpret_cast<const yaml_char_t *>(text.data()), static_cast<int>(text.size()), style);
        if (!id) throw std::bad_alloc();
        return id;
    }
    int add(const Json &node) {
        int id;
        if (node.is_object()) {
            id = yaml_document_add_mapping(&value, reinterpret_cast<const yaml_char_t *>(YAML_MAP_TAG), YAML_BLOCK_MAPPING_STYLE);
            if (!id) throw std::bad_alloc();
            for (auto it = node.begin(); it != node.end(); ++it) {
                int key = scalar(it.key(), YAML_STR_TAG, YAML_DOUBLE_QUOTED_SCALAR_STYLE);
                int child = add(it.value());
                if (!yaml_document_append_mapping_pair(&value, id, key, child)) throw std::bad_alloc();
            }
        } else if (node.is_array()) {
            id = yaml_document_add_sequence(&value, reinterpret_cast<const yaml_char_t *>(YAML_SEQ_TAG), YAML_BLOCK_SEQUENCE_STYLE);
            if (!id) throw std::bad_alloc();
            for (const auto &item : node) {
                int child = add(item);
                if (!yaml_document_append_sequence_item(&value, id, child)) throw std::bad_alloc();
            }
        } else if (node.is_string()) {
            id = scalar(node.get<std::string>(), YAML_STR_TAG, YAML_DOUBLE_QUOTED_SCALAR_STYLE);
        } else {
            const char *tag = node.is_null() ? YAML_NULL_TAG : node.is_boolean() ? YAML_BOOL_TAG :
                              node.is_number_float() ? YAML_FLOAT_TAG : YAML_INT_TAG;
            id = scalar(node.dump(), tag, YAML_PLAIN_SCALAR_STYLE);
        }
        return id;
    }
};
struct YamlWriter {
    yaml_emitter_t emitter{};
    std::string output;
    std::exception_ptr error;
    YamlWriter() {
        if (!yaml_emitter_initialize(&emitter)) throw std::bad_alloc();
        yaml_emitter_set_unicode(&emitter, 1);
        yaml_emitter_set_output(&emitter, [](void *ctx, unsigned char *buffer, std::size_t size) -> int {
            auto &writer = *static_cast<YamlWriter *>(ctx);
            try { writer.output.append(reinterpret_cast<char *>(buffer), size); return 1; }
            catch (...) { writer.error = std::current_exception(); return 0; }
        }, this);
    }
    ~YamlWriter() { yaml_emitter_delete(&emitter); }
    void check(int result) {
        if (error) std::rethrow_exception(error);
        if (!result) throw std::runtime_error("YAML serialization failed");
    }
};
}  // namespace
bool YAMLParser::parse(const std::string &data, ConfigNode &root) {
    try {
        YamlReader reader(data);
        auto parsed = formats::from_json(reader.document());
        root = std::move(parsed);
        return true;
    } catch (const std::runtime_error &) { return false; }
      catch (const std::invalid_argument &) { return false; }
      catch (const std::out_of_range &) { return false; }
}
std::string YAMLParser::dump(const ConfigNode &root) {
    YamlDocument document;
    document.add(formats::to_json(root));
    YamlWriter writer;
    writer.check(yaml_emitter_open(&writer.emitter));
    document.owned = false;  // emitter_dump takes ownership, even on failure.
    writer.check(yaml_emitter_dump(&writer.emitter, &document.value));
    writer.check(yaml_emitter_close(&writer.emitter));
    return writer.output;
}
}  // namespace detail
