cmake_minimum_required(VERSION 3.13)

set(TARGET_NAME config)

add_library(${TARGET_NAME} STATIC)
add_library(${PROJECT_NAME}::${TARGET_NAME} ALIAS ${TARGET_NAME})
if(NOT TARGET basic::config)
  add_library(basic::config ALIAS ${TARGET_NAME})
endif()

target_include_directories(${TARGET_NAME} INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
  $<INSTALL_INTERFACE:include>
)

target_sources(${TARGET_NAME}
  PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/config_parser.cpp")

option(CONFIG_PARSER_ENABLE_JSON "Enable JSON configuration support" ON)
option(CONFIG_PARSER_ENABLE_TOML "Enable TOML configuration support" ON)
option(CONFIG_PARSER_ENABLE_XML "Enable XML configuration support (libxml2)" ON)
option(CONFIG_PARSER_ENABLE_YAML "Enable YAML configuration support (libyaml)" ON)
target_compile_features(${TARGET_NAME} PUBLIC cxx_std_17)
if(CONFIG_PARSER_ENABLE_JSON OR CONFIG_PARSER_ENABLE_TOML OR
   CONFIG_PARSER_ENABLE_XML OR CONFIG_PARSER_ENABLE_YAML)
  find_path(BASIC_JSON_INCLUDE_DIR nlohmann/json.hpp
    HINTS "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/json/include")
  if(NOT BASIC_JSON_INCLUDE_DIR)
    message(FATAL_ERROR "Configuration formats require nlohmann/json.hpp")
  endif()
  target_include_directories(${TARGET_NAME} PRIVATE "${BASIC_JSON_INCLUDE_DIR}")
endif()
if(CONFIG_PARSER_ENABLE_TOML)
  find_path(BASIC_TOML_INCLUDE_DIR toml.hpp
    HINTS "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/tomlplusplus")
  if(NOT BASIC_TOML_INCLUDE_DIR)
    message(FATAL_ERROR "TOML configuration support requires toml.hpp")
  endif()
  target_include_directories(${TARGET_NAME} PRIVATE "${BASIC_TOML_INCLUDE_DIR}")
endif()
foreach(format JSON TOML XML YAML)
  if(CONFIG_PARSER_ENABLE_${format})
    string(TOLOWER "${format}" format_lower)
    target_sources(${TARGET_NAME} PRIVATE "format_${format_lower}.cpp")
    target_compile_definitions(${TARGET_NAME} PUBLIC CONFIG_PARSER_ENABLE_${format})
  endif()
endforeach()
if(CONFIG_PARSER_ENABLE_XML)
  find_package(LibXml2 REQUIRED)
  target_link_libraries(${TARGET_NAME} PRIVATE LibXml2::LibXml2)
endif()
if(CONFIG_PARSER_ENABLE_YAML)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(BASIC_YAML REQUIRED IMPORTED_TARGET yaml-0.1)
  target_link_libraries(${TARGET_NAME} PRIVATE PkgConfig::BASIC_YAML)
endif()
if(BUILD_TESTING)
  add_subdirectory(test)
endif()
