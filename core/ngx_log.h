#ifndef _NGX_LOG_H_
#define _NGX_LOG_H_

#if _WIN32 
#include <stdio.h>
#include <time.h>
#else 
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>
#endif 
#include <mutex>

#include "ngx_queue.h"
#include "ngx_buf.h"



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
	unsigned int			_level;
private:
	std::mutex				_lck;
	ngx_pool_t				*_pool;
	ngx_queue_t				*_wait_logs;
};

unsigned long long	ngx_file_size(const char* file_name);

void ngx_file_rename(const char*  old_file_name, const char* new_file_name);



#define  ngx_log_info(log, fmt, ...)			log->write_level_log(log_level_info, fmt, ##__VA_ARGS__)
#define  ngx_log_error(log, fmt, ...)			log->write_level_log(log_level_error, fmt, ##__VA_ARGS__)
#define  ngx_log_warn(log, fmt, ...)			log->write_level_log(log_level_warn, fmt, ##__VA_ARGS__)
#define  ngx_log_trace(log, fmt, ...)			log->write_level_log(log_level_trace, fmt, ##__VA_ARGS__)
#define  ngx_log_fatal(log, fmt, ...)			log->write_level_log(log_level_fatal, fmt, ##__VA_ARGS__)

#endif
