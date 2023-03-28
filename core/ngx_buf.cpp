#include "ngx_buf.h"
#include <string.h>
#ifdef _WIN32 
#include <windows.h>
#else
#endif

ngx_buf_t * ngx_create_buf(ngx_pool_t *pool, size_t cap)
{
	if (NULL == pool)
		return NULL;
	//
	size_t size = ngx_align(cap + sizeof(ngx_buf_t), NGX_ALIGNMENT);
	ngx_buf_t* buf = (ngx_buf_t*)ngx_palloc(pool, size);
	if (NULL == buf)
		return NULL;
	//
	buf->cap = size - sizeof(ngx_buf_t);
	buf->ref = 1;
	buf->len = 0;
	//buf->data = buf + 1;
	return buf;
}


// ngx_buf_t * ngx_create_buf(ngx_pool_t *pool, void* data, unsigned int len)
// {
// 	if (NULL == pool || nullptr == data || len == 0)
// 		return NULL;
// 	//
// 	size_t cap = ngx_align(len + sizeof(ngx_buf_t), NGX_ALIGNMENT);
// 	ngx_buf_t* buf = (ngx_buf_t*)ngx_palloc(pool, cap);
// 	if (NULL == buf)
// 		return NULL;
// 	//
// 	buf->cap = cap - sizeof(ngx_buf_t);
// 	buf->ref = 1;
// 	buf->len = len;
// 	buf->data = buf + 1;
// 	
// 	//
// 	memcpy(buf->data, data, len);
// 	return buf;
// }

// ngx_buf_t * ngx_buf_append(ngx_pool_t *pool, ngx_buf_t *buf, void* data, unsigned int len)
// {	
// 	ngx_buf_t *tmp;
// 	if (NULL == buf)
// 	{
// 		buf = ngx_create_buf(pool, len);
// 		return buf;
// 	}
// 	//
// 	size_t cap = ngx_align(buf->len + len + sizeof(ngx_buf_t), NGX_ALIGNMENT);
// 	tmp = (ngx_buf_t *)ngx_palloc(pool, cap);
// 	tmp->cap = cap - sizeof(ngx_buf_t);
// 	tmp->len = buf->len + len;
// 	tmp->ref = 1;
// 	tmp->data = tmp + 1;
// 	memcpy(tmp->data, buf->data, buf->len);
// 	memcpy(tmp->data + buf->len, data, len);
// 	return tmp;
// }

void ngx_buf_free(ngx_pool_t *pool, ngx_buf_t* buf)
{
	if (NULL == pool)
		return;
	if (NULL == buf)
		return;
	//
	if (--buf->ref == 0)
		ngx_free(pool, buf);
}

void ngx_buf_add_ref(ngx_buf_t* buf)
{
#ifndef _WIN32 
	buf->ref++;
#else 
	InterlockedIncrement(&buf->ref);
#endif 
}

void ngx_buf_release(ngx_pool_t* pool, ngx_buf_t* buf)
{
#ifndef _WIN32 
	buf->ref--;
#else 
	InterlockedDecrement(&buf->ref);
#endif 
	if (buf->ref == 0) {
		ngx_buf_free(pool, buf);
	}
}
