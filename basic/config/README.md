## Configuration formats

The public entry point is `basic/config/config_parser.h`.

Extended parsers return false on malformed input and leave the output root unchanged.
Successful parses replace the root. Serialization rejects values a format cannot
represent rather than silently dropping them. Extended format conversion limits
nesting to 128 levels. INI and CONF retain their original flat/string semantics.
`ConfigFile::data()` reads the current contents of that file on each call and
returns an owned string.
Their double serialization preserves fixed-point formatting with a default
precision of 6 and allocates enough storage for the formatted result.
`fmt_double_precision` accepts 0 through 1024; other values throw
`std::invalid_argument` before formatting to bound resource use.

The following CMake options default to ON and publish matching preprocessor macros
through the `config` target. Set an option to OFF to omit that parser and dependency.

| Option | Implementation and behavior |
| --- | --- |
| `CONFIG_PARSER_ENABLE_JSON` | Uses the repository's [nlohmann/json](https://github.com/nlohmann/json). Objects, arrays, strings, booleans, finite numbers and null are supported. |
| `CONFIG_PARSER_ENABLE_TOML` | Uses bundled [toml++ v3.4.0](https://github.com/marzer/tomlplusplus/tree/v3.4.0), under its embedded MIT license. Parses TOML 1.0 tables and arrays; dates/times become strings because ConfigNode has no temporal type. Dump requires an object root, rejects null and integers outside int64_t. |
| `CONFIG_PARSER_ENABLE_YAML` | Requires [libyaml](https://github.com/yaml/libyaml) and pkg-config (`yaml-0.1`). Resolves YAML 1.2 core scalar types. Quoted and explicitly tagged strings stay strings. Anchors/aliases are supported, with bounded expansion; recursive/unknown aliases, duplicate keys, multiple documents, custom tags and complex mapping keys are rejected. Merge keys are ordinary keys, not expanded. Non-finite numbers are rejected. |
| `CONFIG_PARSER_ENABLE_XML` | Requires [libxml2](https://gnome.pages.gitlab.gnome.org/libxml2/). Ordinary element names become keys, repeated siblings become arrays, attributes use `@name`, and mixed text uses `#text`; the outer element name is omitted. Dump uses a typed `<config format="basic-config">` representation to preserve arbitrary keys, arrays, null and scalar types. DTDs, user-defined entities, and characters outside XML 1.0 are rejected. |

All extended adapters use nlohmann/json as a conversion utility, even when the JSON
parser is disabled. CMake locates the repository copy or an installed header.

`ConfigValidator` checks a bounded schema vocabulary: `type`, `required`,
`properties`, `items`, `minimum`, `maximum`, `minItems`, `maxItems`, and boolean
`additionalProperties`. Types are object, array, string, boolean, integer, number,
and null. Errors include the configuration path. Unknown rules and malformed
active constraints fail validation. This is not a full JSON Schema implementation.

