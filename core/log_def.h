/**
 * @file	log_def.h
 * @brief 
 * @author	
 * @version 1.0.0 
 * @date	2023-03-01
 */
#ifndef _LOG_DEF_H_
#define _LOG_DEF_H_ 

#include <stdio.h>
#include <stdlib.h> 
#include <time.h>
#include <string.h>

#ifdef SYS_LOG
#ifndef _WIN32 
#include <stdarg.h>
#include <sys/time.h>
#include <unistd.h>
#define  get_file_name(filename)  ((filename == NULL || strrchr(filename, '/') == NULL)?"":strrchr(filename, '/') + 1)
#define trace_print(fmt, ...)			do{ struct timeval tv; gettimeofday(&tv, NULL); struct tm tmNow; localtime_r(&tv.tv_sec, &tmNow);  printf("%04d-%02d-%02d %02d:%02d:%02d.%06ld [TRACE] " fmt, tmNow.tm_year + 1900,tmNow.tm_mon + 1, tmNow.tm_mday, tmNow.tm_hour, tmNow.tm_min, tmNow.tm_sec, tv.tv_usec, ##__VA_ARGS__);} while (0)
#define info_print(fmt, ...)			do{ struct timeval tv; gettimeofday(&tv, NULL); struct tm tmNow; localtime_r(&tv.tv_sec, &tmNow);  printf("%04d-%02d-%02d %02d:%02d:%02d.%06ld [INFO] " fmt, tmNow.tm_year + 1900,tmNow.tm_mon + 1, tmNow.tm_mday, tmNow.tm_hour, tmNow.tm_min, tmNow.tm_sec, tv.tv_usec, ##__VA_ARGS__);} while (0)
#define error_print(fmt, ...)			do { struct timeval tv; gettimeofday(&tv, NULL); struct tm tmNow; localtime_r(&tv.tv_sec, &tmNow);  printf("%04d-%02d-%02d %02d:%02d:%02d.%06ld [ERROR] %s(%d)" fmt, tmNow.tm_year + 1900,tmNow.tm_mon + 1, tmNow.tm_mday, tmNow.tm_hour, tmNow.tm_min, tmNow.tm_sec, tv.tv_usec, get_file_name(__FILE__), __LINE__, ##__VA_ARGS__);} while (0)
#else 
#include <varargs.h> 
#include <sys/timeb.h>
#define  get_file_name(filename)  ((filename == NULL || strrchr(filename, '\\') == NULL)? "": strrchr(filename, '\\') + 1)
#define trace_print(fmt, ...)			do{ timeb tv; ftime(&tv); struct tm tmNow; localtime_s(&tmNow, &tv.time);  printf("%04d-%02d-%02d %02d:%02d:%02d.%03d [TRACE] " fmt, tmNow.tm_year + 1900,tmNow.tm_mon + 1, tmNow.tm_mday, tmNow.tm_hour, tmNow.tm_min, tmNow.tm_sec, tv.millitm, ##__VA_ARGS__); } while (0)
#define info_print(fmt, ...)			do{ timeb tv; ftime(&tv); struct tm tmNow; localtime_s(&tmNow, &tv.time);  printf("%04d-%02d-%02d %02d:%02d:%02d.%03d [INFO] " fmt, tmNow.tm_year + 1900,tmNow.tm_mon + 1, tmNow.tm_mday, tmNow.tm_hour, tmNow.tm_min, tmNow.tm_sec, tv.millitm, ##__VA_ARGS__); } while (0)
#define error_print(fmt, ...)			do{ timeb tv; ftime(&tv); struct tm tmNow; localtime_s(&tmNow, &tv.time);  printf("%04d-%02d-%02d %02d:%02d:%02d.%03d [ERROR] %s(%d) " fmt, tmNow.tm_year + 1900,tmNow.tm_mon + 1, tmNow.tm_mday, tmNow.tm_hour, tmNow.tm_min, tmNow.tm_sec, tv.millitm, get_file_name(__FILE__), __LINE__,##__VA_ARGS__);} while (0)
#endif 
#else 
#define 	trace_print(fmt, ...)		
#define 	info_print(fmt, ...)		
#define 	error_print(fmt, ...)		
#endif 


#endif 
