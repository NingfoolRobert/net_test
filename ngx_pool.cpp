#include "ngx_pool.h"

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
	//
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
//////////////////////////////////////////////////////////////////////////
void ngx_destroy_pool(struct ngx_pool_s *pool)
{
	if (nullptr == pool)
		return;
	//small
	struct ngx_block_s* node = pool->head->next;
	struct ngx_block_s* node_tmp = NULL;
	while (node)
	{
		node_tmp = node;
		node = node->next;
		free(node_tmp);
	}
	pool->head->next = NULL;

	//large 
	struct ngx_large_s* large = pool->large;
	struct ngx_large_s* tmp = NULL;
	while (large)
	{
		tmp = large;
		large = large->next;
		free(tmp);
	}
	pool->large = NULL;

	//
	free(pool);
}

void ngx_reset_pool(struct ngx_pool_s *pool)
{
	//TODO 
}

void* ngx_palloc(struct ngx_pool_s* pool, size_t size)
{
	if (size <= 0)
		return NULL;
	//large
	if (size > PAGE_SIZE - sizeof(struct ngx_block_s))
		return ngx_allocate_large(pool, size);
	//small
	char*  mem_addr = NULL;
	struct ngx_block_s*  curr = pool->curr;
	while (curr)
	{
		mem_addr = (char*)ngx_align_ptr(curr->last, NGX_ALIGNMENT);
		if (curr->end - mem_addr >= size)
		{
			curr->ref++;
			curr->last = mem_addr + size;
			return mem_addr;
		}
		
		curr = curr->next;
	}
	//
	return ngx_allocate_block(pool, size);
}

void ngx_free(struct ngx_pool_s* pool, void* p)
{
	if (nullptr == p || nullptr == pool)
		return;
	
	//small
	struct ngx_block_s* pnode = pool->head;
	while(pnode != nullptr)
	{
		if (p >= pnode->start && p <= pnode->last)
		{
			pnode->ref--;
			//
			if (pnode->ref == 0)
				pnode->last = pnode->last;
			return;
		}
	}
	//large memory 
	struct ngx_large_s* curr = pool->large;
	struct ngx_large_s* next = curr->next;
	struct ngx_large_s* pre = NULL;
	while (curr)
	{
		if (p == curr->data)
		{
			if (NULL == pre)	//
			{
				pool->large = pool->large->next;
				free((char*)p - sizeof(struct ngx_large_s));
				return;
			}
			//
			pre->next = curr->next;
			free((char*)p - sizeof(struct ngx_large_s));
			return;
		}
		pre = curr;
		curr = curr->next;
		next = curr->next;
	}
}

//allocate block
void * ngx_allocate_block(struct ngx_pool_s* pool, size_t size)
{
	char* block = NULL;
#if _WIN32 
	block = (char*)malloc(PAGE_SIZE);
	int ret = block == NULL ? 1 : 0;
#else 
	int ret = posix_memalign((void**)&block, PAGE_SIZE);
#endif 
	if (ret)
		return NULL;
	//
	struct ngx_block_s* new_block = (struct ngx_block_s*)block;
	new_block->start = block + sizeof(struct ngx_block_s);
	new_block->end = block + PAGE_SIZE;
	new_block->last = new_block->start + size;
	new_block->faild = 0;
	new_block->ref = 1;
	new_block->pool = pool;
	new_block->next = NULL;

	struct ngx_block_s*  current = pool->curr;
	struct ngx_block_s*  curr = NULL;
	for (curr = current ; curr->next; curr == curr->next)
	{
		if (curr->faild++ > 4)
			current = curr->next;
	}
	//
	curr->next = new_block;
	pool->curr = current;
	char* ret_addr = new_block->start;
	return ret_addr;
}

//allocate large memory block
void* ngx_allocate_large(struct ngx_pool_s* pool, size_t size)
{
	struct ngx_large_s* node = pool->large;
	while (node->next) node = node->next;
	//
	struct ngx_large_s* large = (struct ngx_large_s*)malloc(size + sizeof(struct ngx_large_s));
	if (nullptr == large)
		return NULL;
	node->next = large;
	large->size = size;
	large->next = nullptr;
	return large->data;
}
