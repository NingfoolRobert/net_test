#ifndef _NGX_LOG_H_
#define _NGX_LOG_H_

#if _WIN32 
#include <stdio.h>
#include <time.h>
#else 
#include <unistd.h>
#include <pthread.h>
#endif 
#include <mutex>
#include "ngx_pool.h"



enum enum_log_level
{
	log_level_all,
	log_level_trace,
	log_level_debug,
	log_level_info,
	log_level_notic,
	log_level_warn,
	log_level_error,
	log_level_fatal,
	log_level_off
};

//typedef struct ngx_log_data_s  ngx_log_data_t;

typedef struct ngx_log_data_s
{
	char				*data;
	size_t				 len;
	ngx_log_data_s		*next;
}ngx_log_data_t;

class ngx_log
{
public:
	explicit ngx_log(const char*  file_name);
	~ngx_log();
public:
	void  write_level_log(unsigned int level, const char* fmt, ...);

	void  write_data(void* data, unsigned int  len);

	void  set_level(int level) { _level = level; }
public:
	void  print_log_file(ngx_log* log_file);
public:
	char					_name[256];
	int						_level;
private:
	std::mutex				_lck;
	ngx_pool_t*				_pool;
	ngx_log_data_t*			_head;			//
};



#define  ngx_log_info(log, fmt, ...)			log->write_level_log(log_level_info, fmt, ##__VA_ARGS__)
#define  ngx_log_error(log, fmt, ...)			log->write_level_log(log_level_error, fmt, ##__VA_ARGS__)
#define  ngx_log_warn(log, fmt, ...)			log->write_level_log(log_level_warn, fmt, ##__VA_ARGS__)
#define  ngx_log_trace(log, fmt, ...)			log->write_level_log(log_level_trace, fmt, ##__VA_ARGS__)
#define  ngx_log_fatal(log, fmt, ...)			log->write_level_log(log_level_fatal, fmt, ##__VA_ARGS__)

#endif
