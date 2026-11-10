#include "format_util.h"

namespace detail {
bool JSONParser::parse(const std::string &data, ConfigNode &root) {
    try {
        auto value = formats::Json::parse(data, [](int depth, formats::Json::parse_event_t,
                                                 formats::Json &) {
            formats::check_depth(static_cast<std::size_t>(depth));
            return true;
        });
        auto parsed = formats::from_json(value);
        root = std::move(parsed);
        return true;
    } catch (const formats::Json::exception &) { return false; }
      catch (const std::runtime_error &) { return false; }
}
std::string JSONParser::dump(const ConfigNode &root) {
    return formats::to_json(root).dump(2);
}
}  // namespace detail
