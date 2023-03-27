#ifndef _NGX_BUF_H_
#define _NGX_BUF_H_

#include <stdio.h>

#include "ngx_pool.h"

typedef struct ngx_buf_s  ngx_buf_t;

struct ngx_buf_s
{
	size_t					 cap;
	size_t					 len;
	size_t					 ref;
	ngx_pool_t				 *pool;
	char					 data[];
};

ngx_buf_t *ngx_create_buf(ngx_pool_t *pool, size_t cap);

/*ngx_buf_t *ngx_create_buf(ngx_pool_t *pool, void* data, unsigned int len);*/

/*ngx_buf_t *ngx_buf_append(ngx_pool_t *pool, ngx_buf_t *buf, void* data, unsigned int len);*/

void	ngx_buf_free(ngx_pool_t *pool, ngx_buf_t* buf);

void	ngx_buf_add_ref(ngx_buf_t* buf);

void	ngx_buf_release(ngx_pool_t* pool, ngx_buf_t* buf);

#endif 