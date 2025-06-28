cmake_minimum_required(VERSION 3.13)

#message(STATUS "Found CZMQ")

find_path(CZMQ_INCLUDE_DIR  zpoller.h zrex.h zframe.h 
  HINTS ${CMAKE_CURRENT_LIST_DIR}/../include)
  
message(STATUS "CZMQ_INCLUDE_DIR: ${CZMQ_INCLUDE_DIR}")

find_library(CZMQ_STATIC_LIBRARY NAMES  libczmq.a PATHS ${CMAKE_CURRENT_LIST_DIR}/../lib)
message(STATUS "Found STATIC CZMQ: ${CZMQ_STATIC_LIBRARY}")
find_library(CZMQ_LIBRARY NAMES  czmq HINTS ${CMAKE_CURRENT_LIST_DIR}/../lib)


include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(czmq DEFAULT_MSG  CZMQ_LIBRARY CZMQ_INCLUDE_DIR)


if(CZMQ_FOUND)
  SET(CZMQ_INCLUDE_DIRS  ${CZMQ_INCLUDE_DIR})
  SET(CZMQ_LIBRARIES  ${CZMQ_LIBRARY})
  add_library(czmq::czmq  UNKNOWN IMPORTED)
  set_target_properties(czmq::czmq  PROPERTIES 
    IMPORTED_LOCATION  "${CZMQ_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES  "${CZMQ_INCLUDE_DIR}")
  #targets_compile_options(czmq::czmq PRIVATE  -DSOME_STATIC_DEFINE)
  
  add_library(czmq::czmq_static  UNKNOWN IMPORTED)
  set_target_properties(czmq::czmq_static  PROPERTIES 
    IMPORTED_LOCATION  "${CZMQ_STATIC_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES  "${CZMQ_INCLUDE_DIR}")
  message(STATUS "Found CZMQ: ${CZMQ_LIBRARY}")
endif()


mark_as_advanced(CZMQ_INCLUDE_DIR CZMQ_LIBRARY)
