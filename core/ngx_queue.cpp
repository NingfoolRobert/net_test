#include "ngx_queue.h"
#include <string.h>

ngx_queue_t *ngx_create_queue(ngx_pool_t *pool, size_t size, size_t n) {
    size_t s = ngx_align(sizeof(ngx_queue_t) + size * n, NGX_ALIGNMENT);
    ngx_queue_t *q = (ngx_queue_t *)ngx_palloc(pool, s);
    //
    q->elts = (char *)q + sizeof(ngx_queue_t);
    q->size = size;
    q->nalloc = n;
    q->head = 0;
    q->end = 0;
    return q;
}

void *ngx_queue_get(ngx_queue_t *q) {
    if (ngx_queue_empty(q)) {
        return NULL;
    }
    //
    if (q->head >= q->nalloc)
        q->head = 0;
    //
    return (char *)(q->elts) + q->head++ * q->size;
}

bool ngx_queue_push(ngx_queue_t *q, void *val) {
    if (NULL == q || NULL == val)
        return false;
    //
    if (q->nalloc == ngx_queue_size(q))
        return false;
    //
    if (q->end >= q->nalloc)
        q->end = 0;
    //
    memcpy((char *)(q->elts) + q->size * q->end, val, q->size);
    q->end++;
    return true;
}

size_t ngx_queue_size(ngx_queue_t *q) {
    if (NULL == q)
        return 0;
    //
    if (q->end < q->head)
        return q->end + q->nalloc - q->head;
    //
    return q->end - q->head;
}

bool ngx_queue_empty(ngx_queue_t *q) {
    if (NULL == q)
        return false;
    //
    return q->end == q->head;
}
