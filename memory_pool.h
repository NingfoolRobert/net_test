#ifndef _MEMORY_POOL_H_
#define _MEMORY_POOL_H_

#include <stdio.h>
#include <stdlib.h>


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



void*  ngx_allocate(struct ngx_pool_s* pool, size_t  size);

void  ngx_free(struct ngx_pool_s* pool, void* p);

// void   ngx_free_node(struct ngx_pool_s *pool, struct ngx_node_s* node);
// 
// void  add_ref(struct ngx_node_s*  node);
// 
// void  release(struct ngx_node_s* node);

#endif
