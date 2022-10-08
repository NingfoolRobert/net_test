#include "ngx_log.h"
#include <string.h>
#include <stdarg.h>
#ifdef _WIN32 
#include <sys/timeb.h>
#endif 

static const char*   log_level_prefix[] = { "", "[TRACE] ", "[DEBUG] ", "[INFO] ", "[NOTIC] ", "[WARN] ", "[ERROR] ", "[FATAL] " };


unsigned long long ngx_file_size(const char* file_name)
{
	FILE*  pFile = fopen(file_name, "r");
	if (pFile == NULL)
		return 0;
	unsigned long long size = ftell(pFile);
	fclose(pFile);
	return size;
}

void ngx_file_rename(const char* old_file_name, const char* new_file_name)
{
	rename(old_file_name, new_file_name);
}

ngx_log::ngx_log(const char* file_name) :_level(log_level_info), _wait_logs(NULL)
{
	if(file_name) strcpy(_name, file_name);
	_pool = ngx_create_pool(1);
	_wait_logs = ngx_create_queue(_pool, sizeof(ngx_buf_t*), 4096);
}

ngx_log::~ngx_log()
{
	ngx_destroy_pool(_pool);
	_wait_logs = NULL;
}

void ngx_log::write_level_log(unsigned int level, const char* fmt, ...)
{
	std::unique_lock<std::mutex> _(_lck);
	if (level < _level || level >= log_level_off)
		return;
	
	char szTmp[1024] = { 0 };
#ifdef _WIN32
	timeb tbNow;
	ftime(&tbNow);
	struct tm tmNow;
	localtime_s(&tmNow, &tbNow.time);
	sprintf(szTmp, "%04d-%02d-%02d %02d:%02d:%02d.%03d %s", tmNow.tm_year + 1900, tmNow.tm_mon + 1, tmNow.tm_mday, tmNow.tm_hour, tmNow.tm_min, tmNow.tm_sec, tbNow.millitm, log_level_prefix[level]);
#else 
	struct timeval tv;
	gettimeofday(&tv, NULL);
	struct tm tmNow;
	localtime_r(&tv.tv_sec, &tmNow);
	sprintf(szTmp, "%04d-%02d-%02d %02d:%02d:%02d.%03d %s", tmNow.tm_year + 1900, tmNow.tm_mon + 1, tmNow.tm_mday, tmNow.tm_hour, tmNow.tm_min, tmNow.tm_sec, tv.tv_usec / 1000, log_level_prefix[level]);
#endif 
	//
	unsigned int prefix_len = strlen(szTmp);
	va_list args;
	va_start(args, fmt);
	unsigned int nLen = vsnprintf(NULL, 0, fmt, args);
	nLen += prefix_len;
	ngx_buf_t* buf = ngx_create_buf(_pool, nLen + 3);
	if (NULL == buf)
	{
		va_end(args);
		return;
	}
	//
	memcpy(buf->data, szTmp, prefix_len);
	vsnprintf((char*)(buf->data) + prefix_len, nLen - prefix_len, fmt, args);
	//

#ifdef _WIN32
	strcpy((char*)(buf->data) + nLen, "\r\n");
	buf->len = nLen + 2;
#else 
	strcpy((char*)(buf->data) + nLen, "\n");
	buf->len = nLen + 1;
#endif
	va_end(args); 
	//
	{
		if (_wait_logs->nalloc > ngx_queue_size(_wait_logs))
			ngx_queue_push(_wait_logs, &buf);
		else
			printf("push log fail, %s", buf->data);
	}
}

void ngx_log::write_data(void* data, unsigned int len)
{
	std::unique_lock<std::mutex> _(_lck);
	ngx_buf_t* buf = (ngx_buf_t*)ngx_create_buf(_pool, len);
	if (NULL == buf)
		return;
	
	memcpy(buf->data, data, len);
	if (!ngx_queue_push(_wait_logs, &buf))
		ngx_buf_free(_pool, buf);
}

void ngx_log::print_log_file(ngx_log* log_file)
{
	std::unique_lock<std::mutex> _(_lck);
	ngx_buf_t *buf = NULL;
	//
	FILE* pFile = fopen(_name, "ab+");
	if (NULL == pFile)
		return;
	while (!ngx_queue_empty(_wait_logs))
	{
		buf = *(ngx_buf_t**)ngx_queue_get(_wait_logs);
		if (NULL == buf)
			break;
		
		fwrite(buf->data, 1, buf->len, pFile);
		ngx_free(_pool, buf);
	}
	fclose(pFile);
}
