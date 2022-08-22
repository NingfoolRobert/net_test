#include "ngx_buf.h"
#include <string.h>


ngx_buf_t * ngx_create_buf(ngx_pool_t *pool, size_t cap)
{
	if (NULL == pool)
		return NULL;
	//
	ngx_buf_t* buf = (ngx_buf_t*)ngx_palloc(pool, cap + sizeof(ngx_buf_t));
	if (NULL == buf)
		return NULL;
	//
	buf->cap = cap;
	buf->ref = 1;
	buf->len = 0;

	return buf;
}


ngx_buf_t * ngx_buf_append(ngx_pool_t *pool, ngx_buf_t *buf, void* data, unsigned int len)
{	
	ngx_buf_t *tmp;
	size_t cap = ngx_align(len + sizeof(ngx_buf_t), NGX_ALIGNMENT);
	if (NULL == buf)
	{
		tmp = (ngx_buf_t *)ngx_palloc(pool, cap);
		if (NULL == tmp)
			return NULL;
		//
		tmp->cap = cap - sizeof(ngx_buf_t);
		tmp->ref = 1;
		tmp->len = len;
		memcpy(tmp->data, data, len);
		return tmp;
	}
	//TODO 
	
	
	return NULL;
}

void ngx_buf_release(ngx_pool_t *pool, ngx_buf_t* buf)
{
	if (NULL == pool)
		return;
	if (NULL == buf)
		return;
	//
	if (--buf->ref == 0)
		ngx_free(pool, buf);
}
