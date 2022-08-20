#ifndef _NGX_LIST_H_
#define _NGX_LIST_H_

#include <stdio.h>
#include "ngx_pool.h"


typedef struct ngx_list_part_s  ngx_list_part_t;

struct ngx_list_part_s
{
	void				*elts;
	size_t				nelts;
	ngx_list_part_s		*next;
};


typedef struct {
	ngx_list_part_t				*last;
	ngx_list_part_t				 part;
	size_t						 size;
	size_t						 nalloc;
	ngx_pool_t					*pool;
}ngx_list_t;


ngx_list_t *ngx_create_create(ngx_pool_t *pool, size_t n, size_t size);

static inline size_t  ngx_list_init(ngx_list_t *list, ngx_pool_t *pool, size_t n, size_t  size) {
	list->part.elts = ngx_palloc(pool, n * size);
	if (list->part.elts == NULL)
		return -1;
	
	list->part.nelts = 0;
	list->part.next = NULL;
	list->last = &list->part;
	list->nalloc = n;
	list->pool = pool;
	return 0;
}





#endif
