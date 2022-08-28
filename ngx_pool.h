#ifndef _NGX_POOL_H_
#define _NGX_POOL_H_

#include <stdio.h>
#include <stdlib.h>

#define	NGX_ALIGNMENT						8
//#define ngx_align(n, alignment)				(((n) + (alignment - 1)) & ~(alignment -1))
#define ngx_align(n, alignment)					(((n)+(alignment - 1)) & ~(alignment - 1))
#define ngx_align_ptr(p, alignment)			(void*)((((size_t)p) + (alignment - 1)) & ~(alignment - 1))	

struct ngx_pool_s;
typedef struct ngx_pool_s  ngx_pool_t;
struct  ngx_block_s {
	char				*start;
	char				*end;
	char				*last;
	unsigned int		ref;			//
	unsigned int		faild;			//
	ngx_block_s			*next;
	ngx_pool_t			*pool;
};
//
struct ngx_large_s {
	struct	ngx_large_s			*next;
	size_t						size;
	char						data[];
};
//
struct ngx_pool_s {
	struct   ngx_large_s			*large;
	struct   ngx_block_s			*head;
	struct   ngx_block_s			*curr;
};

struct ngx_pool_s *ngx_create_pool(size_t size);

void  ngx_destroy_pool(struct ngx_pool_s *pool);

void  ngx_reset_pool(struct ngx_pool_s *pool);

void*  ngx_palloc(struct ngx_pool_s* pool, size_t  size);

void  ngx_free(struct ngx_pool_s* pool, void* p);

void *ngx_allocate_block(struct ngx_pool_s* pool, size_t size);

void *ngx_allocate_large(struct ngx_pool_s* pool, size_t size);


#endif
