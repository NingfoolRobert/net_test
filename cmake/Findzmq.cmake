cmake_minimum_required(VERSION 3.13)

find_path(ZMQ_INCLUDE_DIR  zmq.h
  HINTS ${CMAKE_CURRENT_LIST_DIR}/../include)

message(STATUS "ZMQ_INCLUDE_DIR: ${ZMQ_INCLUDE_DIR}")

find_library(ZMQ_LIBRARY NAMES  zmq  HINTS  ${CMAKE_CURRENT_LIST_DIR}/../lib)
#message(STATUS "ZMQ_LIBRARY: ${ZMQ_LIBRARY}")



include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(zmq DEFAULT_MSG ZMQ_LIBRARY ZMQ_INCLUDE_DIR)


if(ZMQ_FOUND)
  SET(ZMQ_INCLUDE_DIRS  ${ZMQ_INCLUDE_DIR})
  SET(ZMQ_LIBRARIES  ${ZMQ_LIBRARY})
  add_library(zmq::zmq  UNKNOWN IMPORTED)
  set_target_properties(zmq::zmq  PROPERTIES 
    IMPORTED_LOCATION  "${ZMQ_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES  "${ZMQ_INCLUDE_DIR}")
  
  message(STATUS "Found ZMQ: ${ZMQ_LIBRARY}")
else()
  message(FATAL_ERROR  "unable to find libzmq")
endif()


mark_as_advanced(ZMQ_INCLUDE_DIR ZMQ_LIBRARY)
