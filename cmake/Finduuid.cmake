cmake_minimum_required(VERSION 3.13)


find_path(UUID_INCLUDE_DIR  uuid.h 
  HINTS ${CMAKE_CURRENT_LIST_DIR}/../include/uuid)
  
message(STATUS "UUID_INCLUDE_DIR: ${UUID_INCLUDE_DIR}")

find_library(UUID_STATIC_LIBRARY NAMES libuuid.a PATHS ${CMAKE_CURRENT_LIST_DIR}/../lib)
message(STATUS "Found STATIC UUID: ${UUID_STATIC_LIBRARY}")
find_library(UUID_LIBRARY NAMES uuid HINTS ${CMAKE_CURRENT_LIST_DIR}/../lib)


include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(uuid DEFAULT_MSG UUID_LIBRARY UUID_INCLUDE_DIR)


if(UUID_FOUND)
  SET(UUID_INCLUDE_DIRS  ${UUID_INCLUDE_DIR})
  SET(UUID_LIBRARIES  ${UUID_LIBRARY})
  add_library(uuid::uuid  UNKNOWN IMPORTED)
  set_target_properties(uuid::uuid  PROPERTIES 
    IMPORTED_LOCATION  "${UUID_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES  "${UUID_INCLUDE_DIR}")
  
  add_library(uuid::uuid_static  UNKNOWN IMPORTED)
  set_target_properties(uuid::uuid_static  PROPERTIES 
    IMPORTED_LOCATION  "${UUID_STATIC_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES  "${UUID_INCLUDE_DIR}")
  message(STATUS "Found UUID: ${UUID_LIBRARY}")
endif()


mark_as_advanced(UUID_INCLUDE_DIR UUID_LIBRARY)
