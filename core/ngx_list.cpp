#include "ngx_list.h"


ngx_list_t * ngx_create_create(ngx_pool_t *pool, size_t n, size_t size)
{
	ngx_list_t  *list;

	list = (ngx_list_t*)ngx_palloc(pool, sizeof(ngx_list_t));
	if (list == NULL) {
		return NULL;
	}

	if (ngx_list_init(list, pool, n, size) != 0) {
		return NULL;
	}

	return list;
}
