#include "../core/ngx_buf.h"
#include "../core/ngx_pool.h"

#include <thread>



int main()
{
	ngx_pool_s* pool = ngx_create_pool(1024);	
	if(NULL == pool)
	{
		printf("memory error.\n");
		return -1;
	}
	
	int size = 1000;
	while(size--)
	{
		char* buf = (char*)ngx_palloc(pool, 1024);
		sprintf(buf, "hello world %d...", size);
		printf("%s\n", buf);
		ngx_free(pool, buf);
	}
	

	return 0;	
}
