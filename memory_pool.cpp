#include "memory_pool.h"

#define PAGE_SIZE  4096

struct ngx_pool_s * ngx_create_pool(size_t size)
{
	struct ngx_pool_s* pool;
	if (size < PAGE_SIZE || size % PAGE_SIZE != 0)
		size = PAGE_SIZE;
	
	//
#if _WIN32 
	pool = (struct ngx_pool_s*)malloc(PAGE_SIZE);
	int ret = pool == NULL ? 1 : 0;
#else 
	int ret = posix_memalign((void**)&pool, MP_ALIGNMENT, size);
#endif
	if (ret)
		return NULL;

	pool->large = NULL;
	pool->curr = pool->head = (struct ngx_block_s*)((char*)pool + sizeof(struct ngx_pool_s));
	pool->head->last = (char*)pool + sizeof(struct ngx_pool_s) + sizeof(struct ngx_block_s);
	pool->head->end = (char*)pool + PAGE_SIZE;
	pool->head->start = pool->head->last;
	pool->head->faild = 0;
	pool->head->ref = 0;
	pool->head->next = nullptr;
	pool->head->pool = pool;

	return pool;
}


// 	struct ngx_pool_s* pool = (struct ngx_pool_s*)malloc(sizeof(struct ngx_pool_s));
// 	pool->curr = nullptr;
// 	pool->head = nullptr;
// 	pool->large = nullptr;
// 
// 	struct ngx_block_s* pTmp = pool->head;
// 	for (auto i = 0; i < size; ++i)
// 	{
// 		struct ngx_block_s*  ptr = (struct ngx_block_s*)malloc(PAGE_SIZE);
// 		ptr->start = (char*)ptr + sizeof(struct ngx_block_s);
// 		ptr->end = (char*)ptr + PAGE_SIZE;
// 		ptr->last = ptr->start;
// 		ptr->ref = 0;
// 		ptr->faild = 0;
// 		ptr->pool = pool;
// 		ptr->next = nullptr;
// 		//
// 		pTmp->next = ptr;
// 		pTmp = ptr;
// 	}
// 	//
void ngx_destroy_pool(struct ngx_pool_s *pool)
{
	
}

void* ngx_allocate(struct ngx_pool_s* pool, size_t size)
{
	return NULL;
}

void ngx_free(struct ngx_pool_s* pool, void* p)
{

}
