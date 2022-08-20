#include "ngx_log.h"

#include <stdarg.h>
#ifdef _WIN32 
#include <sys/timeb.h>
#endif 


ngx_log*			g_log_ptr = nullptr;

static const char*   log_level_prefix[] = { "", "[TRACE] ", "[DEBUG] ", "[INFO] ", "[NOTIC] ", "[WARN] ", "[ERROR] ", "[FATAL] " };


ngx_log::ngx_log(const char* file_name) :_level(log_level_info),  _head(NULL)
{
	g_log_ptr = this;
	if(file_name)	strcpy(_name, file_name);
	_pool = ngx_create_pool(1);
}

ngx_log::~ngx_log()
{
	g_log_ptr = nullptr;
	ngx_destroy_pool(_pool);
	_head = NULL;
}

void ngx_log::write_level_log(unsigned int level, const char* fmt, ...)
{
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
	get_time_of(&tv, NULL);
	struct tm tmNow;
	localtime_s(&tmNow, &tv.tv_sec);
	sprintf(szTmp, "%04d-%02d-%02d %02d:%02d:%02d.%03d %s", tmNow.tm_year + 1900, tmNow.tm_mon + 1, tmNow.tm_mday, tmNow.tm_hour, tmNow.tm_min, tmNow.tm_sec, tv.tv_usec / 1000, log_level_prefix[level]);
#endif 
	//
	unsigned int prefix_len = strlen(szTmp);
	va_list args;
	va_start(fmt, args);
	unsigned int nLen = vsnprintf(NULL, 0, fmt, args);
	nLen += prefix_len;
	ngx_log_data_t* log = (ngx_log_data_t*)ngx_palloc(_pool, sizeof(ngx_log_data_t));
	if (NULL == log)
	{
		va_end(args);
		return;
	}
#ifdef _WIN32
	log->data = (char*)ngx_palloc(_pool, nLen + 3);
	log->len = nLen + 2;
#else 
	log->data = ngx_palloc(_pool, nLen + 2);
	log->len = nLen + 1;
#endif
	
	log->next = NULL;
	memcpy(log->data, szTmp, prefix_len);
	//
	vsnprintf((char*)(log->data) + prefix_len, nLen - prefix_len, fmt, args);
	va_end(args);
	
#ifdef _WIN32
	strcpy((char*)(log->data) + nLen, "\r\n");
#else 
	strcpy((char*)(log->data) + nLen, "\n");
#endif 
	{
		if (_head == NULL)
		{
			_head = log;
		}
		else
		{
			ngx_log_data_t *pre = _head;
			while (pre->next)  pre = pre->next;
			pre->next = log;
		}
	}
}

void ngx_log::print_log_file(ngx_log* log_file)
{
	if (NULL == _head || NULL == _head->data)
		return;
	// = _head;
	FILE* pFile = fopen(_name, "ab+");
	if (NULL == pFile)
		return;
	//
	ngx_log_data_t *pre, *next;
	pre = _head;
	while(pre)
	{ 
		next = pre->next;
		fwrite(pre->data, 1, pre->len, pFile);
		//
		ngx_free(_pool, pre->data);
		_head = next;
		ngx_free(_pool, pre);
		pre = next;
	}
	fclose(pFile);
}
