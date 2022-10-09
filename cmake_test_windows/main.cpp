
#include <stdio.h> 
#include <stdlib.h> 

#ifndef _WIN32 
#include <unistd.h> 
#define ngx_sleep	usleep
#else 
#include <windows.h>
#define ngx_sleep  Sleep 
#endif 

int main()
{
	while(true)
	{
		ngx_sleep(1000);
	}
	return 0;
}
