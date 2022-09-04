#ifndef _NGX_QUEUE_H_
#define _NGX_QUEUE_H_

#include "ngx_pool.h"

typedef struct ngx_queue_s ngx_queue_t;
//
struct ngx_queue_s
{
	void		*elts;
	size_t		size;
	size_t		nalloc;

	size_t		head;
	size_t		end;
};
//
ngx_queue_t * ngx_create_queue(ngx_pool_t *pool, size_t size, size_t n);
//
void*	ngx_queue_get(ngx_queue_t* q);
//
bool	ngx_queue_push(ngx_queue_t *q, void* val);
//
size_t	ngx_queue_size(ngx_queue_t *q);
//
bool	ngx_queue_empty(ngx_queue_t *q);
//

#endif
