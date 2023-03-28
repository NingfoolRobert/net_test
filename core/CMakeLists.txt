cmake_minimum_required(VERSION 3.10)

set(TARGET_NAME  core)


add_definitions(-DSYS_LOG)
add_library(${TARGET_NAME} INTERFACE)
add_library(${PROJECT_NAME}::${TARGET_NAME} ALIAS ${TARGET_NAME})

target_include_directories(${TARGET_NAME}
	INTERFACE 
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)


target_sources(${TARGET_NAME}
	INTERFACE 
	${CMAKE_CURRENT_SOURCE_DIR}/ngx_pool.cpp 
	${CMAKE_CURRENT_SOURCE_DIR}/ngx_log.cpp 
	${CMAKE_CURRENT_SOURCE_DIR}/eventloop.cpp 
	${CMAKE_CURRENT_SOURCE_DIR}/eventloop_busy.cpp 
	${CMAKE_CURRENT_SOURCE_DIR}/eventloop_perf.cpp 
	${CMAKE_CURRENT_SOURCE_DIR}/eventloop_epoll.cpp 
	${CMAKE_CURRENT_SOURCE_DIR}/net_io.cpp 
	${CMAKE_CURRENT_SOURCE_DIR}/ngx_buf.cpp 
	${CMAKE_CURRENT_SOURCE_DIR}/ngx_core.cpp 
	${CMAKE_CURRENT_SOURCE_DIR}/ngx_list.cpp 
	${CMAKE_CURRENT_SOURCE_DIR}/ngx_queue.cpp 
	${CMAKE_CURRENT_SOURCE_DIR}/tcp_conn.cpp 
	${CMAKE_CURRENT_SOURCE_DIR}/ngx_acceptor.cpp 
	${CMAKE_CURRENT_SOURCE_DIR}/eventthread.cpp 
	${CMAKE_CURRENT_SOURCE_DIR}/time.cpp 
	)
