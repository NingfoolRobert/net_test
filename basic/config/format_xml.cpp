#include "format_util.h"
#include <libxml/chvalid.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <climits>
#include <memory>
#include <unordered_map>

namespace detail {
namespace {
using formats::Json;
using XmlDocument = std::unique_ptr<xmlDoc, decltype(&xmlFreeDoc)>;
struct XmlString {
    xmlChar *value;
    ~XmlString() { xmlFree(value); }
    std::string str() const { return value ? reinterpret_cast<const char *>(value) : ""; }
};
std::string attribute(xmlNode *node, const char *name) {
    XmlString value{xmlGetProp(node, BAD_CAST name)};
    return value.str();
}
std::string content(xmlNode *node) {
    XmlString value{xmlNodeGetContent(node)};
    return value.str();
}
void validate_string(const std::string &text) {
    if (text.size() > static_cast<std::size_t>(INT_MAX) || text.find('\0') != std::string::npos ||
        !xmlCheckUTF8(BAD_CAST text.c_str())) throw std::invalid_argument("Invalid XML string");
    const auto *ptr = reinterpret_cast<const unsigned char *>(text.data());
    std::size_t left = text.size();
    while (left) {
        int count = static_cast<int>(std::min<std::size_t>(left, 4));
        int codepoint = xmlGetUTF8Char(ptr, &count);
        if (count <= 0 || codepoint < 0 || !xmlIsCharQ(codepoint))
            throw std::invalid_argument("XML 1.0 cannot represent this character");
        ptr += count;
        left -= static_cast<std::size_t>(count);
    }
}
Json read_xml(xmlNode *node, bool typed, std::size_t depth = 0) {
    formats::check_depth(depth);
    const auto type = typed ? attribute(node, "type") : "";
    Json result;
    if (typed) {
        if (type == "object") result = Json::object();
        else if (type == "array") result = Json::array();
        else {
            for (auto *child = node->children; child; child = child->next)
                if (child->type == XML_ELEMENT_NODE || child->type == XML_ENTITY_REF_NODE)
                    throw std::runtime_error("Invalid XML scalar");
            const auto text = content(node);
            if (type == "string") return text;
            if (type == "null" && text.empty()) return nullptr;
            if (type == "boolean" || type == "number") {
                auto scalar = Json::parse(text);
                if ((type == "boolean" && scalar.is_boolean()) || (type == "number" && scalar.is_number()))
                    return scalar;
            }
            throw std::runtime_error("Invalid typed XML value");
        }
        for (auto *child = node->children; child; child = child->next) {
            if (child->type == XML_COMMENT_NODE || child->type == XML_PI_NODE) continue;
            if (child->type == XML_TEXT_NODE && content(child).find_first_not_of(" \t\r\n") == std::string::npos) continue;
            if (child->type != XML_ELEMENT_NODE) throw std::runtime_error("Invalid XML container");
            if (type == "array") {
                if (!xmlStrEqual(child->name, BAD_CAST "item")) throw std::runtime_error("Expected XML item");
                result.push_back(read_xml(child, true, depth + 1));
            } else {
                if (!xmlStrEqual(child->name, BAD_CAST "entry") || !xmlHasProp(child, BAD_CAST "key"))
                    throw std::runtime_error("Expected XML entry with a key");
                auto key = attribute(child, "key");
                if (result.contains(key)) throw std::runtime_error("Duplicate XML key");
                result[key] = read_xml(child, true, depth + 1);
            }
        }
        return result;
    }
    result = Json::object();
    for (auto *attr = node->properties; attr; attr = attr->next) {
        XmlString value{xmlNodeListGetString(node->doc, attr->children, 1)};
        result["@" + std::string(reinterpret_cast<const char *>(attr->name))] = value.str();
    }
    std::string text;
    std::unordered_map<std::string, std::size_t> counts;
    for (auto *child = node->children; child; child = child->next) {
        if (child->type == XML_ENTITY_REF_NODE) throw std::runtime_error("XML entities are unsupported");
        if (child->type == XML_TEXT_NODE || child->type == XML_CDATA_SECTION_NODE) text += content(child);
        if (child->type != XML_ELEMENT_NODE) continue;
        auto key = std::string(reinterpret_cast<const char *>(child->name));
        auto value = read_xml(child, false, depth + 1);
        auto &count = counts[key];
        if (count == 0) result[key] = std::move(value);
        else {
            if (count == 1) result[key] = Json::array({result[key]});
            result[key].push_back(std::move(value));
        }
        ++count;
    }
    if (result.empty()) return text;
    if (text.find_first_not_of(" \t\r\n") != std::string::npos) result["#text"] = text;
    return result;
}
void write_xml(xmlNode *node, const Json &value) {
    const char *type = value.is_object() ? "object" : value.is_array() ? "array" : value.is_null() ? "null" :
                       value.is_string() ? "string" : value.is_boolean() ? "boolean" : "number";
    if (!xmlNewProp(node, BAD_CAST "type", BAD_CAST type)) throw std::bad_alloc();
    if (value.is_object()) {
        for (auto it = value.begin(); it != value.end(); ++it) {
            validate_string(it.key());
            auto *child = xmlNewChild(node, nullptr, BAD_CAST "entry", nullptr);
            if (!child || !xmlNewProp(child, BAD_CAST "key", BAD_CAST it.key().c_str())) throw std::bad_alloc();
            write_xml(child, it.value());
        }
    } else if (value.is_array()) {
        for (const auto &item : value) {
            auto *child = xmlNewChild(node, nullptr, BAD_CAST "item", nullptr);
            if (!child) throw std::bad_alloc();
            write_xml(child, item);
        }
    } else if (!value.is_null()) {
        const auto text = value.is_string() ? value.get<std::string>() : value.dump();
        validate_string(text);
        auto *child = xmlNewTextLen(BAD_CAST text.data(), static_cast<int>(text.size()));
        if (!child || !xmlAddChild(node, child)) throw std::bad_alloc();
    }
}
}  // namespace
bool XMLParser::parse(const std::string &data, ConfigNode &root) {
    if (data.size() > static_cast<std::size_t>(INT_MAX)) return false;
    try {
        XmlDocument document(xmlReadMemory(data.data(), static_cast<int>(data.size()), nullptr, nullptr,
            XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING), xmlFreeDoc);
        if (!document || document->intSubset || document->extSubset) return false;
        auto *element = xmlDocGetRootElement(document.get());
        if (!element) return false;
        const bool typed = attribute(element, "format") == "basic-config";
        auto parsed = formats::from_json(read_xml(element, typed));
        root = std::move(parsed);
        return true;
    } catch (const Json::exception &) { return false; }
      catch (const std::runtime_error &) { return false; }
}
std::string XMLParser::dump(const ConfigNode &root) {
    const auto value = formats::to_json(root);
    XmlDocument document(xmlNewDoc(BAD_CAST "1.0"), xmlFreeDoc);
    if (!document) throw std::bad_alloc();
    auto *element = xmlNewNode(nullptr, BAD_CAST "config");
    if (!element) throw std::bad_alloc();
    xmlDocSetRootElement(document.get(), element);
    if (!xmlNewProp(element, BAD_CAST "format", BAD_CAST "basic-config")) throw std::bad_alloc();
    write_xml(element, value);
    xmlChar *buffer = nullptr;
    int size = 0;
    xmlDocDumpFormatMemoryEnc(document.get(), &buffer, &size, "UTF-8", 1);
    XmlString output{buffer};
    if (!buffer) throw std::runtime_error("XML serialization failed");
    return std::string(reinterpret_cast<const char *>(buffer), static_cast<std::size_t>(size));
}
}  // namespace detail
