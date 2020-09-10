/*
 * Copyright (c) 2020 NVIDIA Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <config.h>

#include "openvswitch/list.h"
#include "openvswitch/thread.h"
#include "openvswitch/util.h"
#include "ovs-atomic.h"
#include "seq-pool.h"

struct seq_node {
    struct ovs_list list_node;
    uint32_t id;
};

#define SEQPOOL_C_SIZE 32
#define SEQPOOL_C_MASKED(u) (u & (SEQPOOL_C_SIZE - 1))
#define SEQPOOL_C_EMPTY(c) ((c)->head == (c)->tail)
#define SEQPOOL_C_FULL(c) (!SEQPOOL_C_EMPTY(c) \
                      && SEQPOOL_C_MASKED((c)->head) \
                      == SEQPOOL_C_MASKED((c)->tail))
#define SEQPOOL_C_ADD(c, u) do { (c)->ids[SEQPOOL_C_MASKED((c)->head++)] = u; } while(0)
#define SEQPOOL_C_POP(c) ((c)->ids[SEQPOOL_C_MASKED((c)->tail++)])
BUILD_ASSERT_DECL(IS_POW2(SEQPOOL_C_SIZE));

struct seq_pool_cache {
    struct ovs_mutex c_lock;
    uint32_t head;
    uint32_t tail;
    uint32_t ids[SEQPOOL_C_SIZE];
};

struct seq_pool {
    uint32_t next_id;
    struct seq_pool_cache *cache; /* per-user id cache. */
    size_t nb_user; /* Number of user threads. */
    struct ovs_mutex lock; /* Protects free_ids access. */
    struct ovs_list free_ids; /* Set of currently free IDs. */
    uint32_t base; /* IDs in the range of [base, base + n_ids). */
    uint32_t n_ids; /* Total number of ids in the pool. */
};

struct seq_pool *
seq_pool_create(unsigned int nb_user, uint32_t base, uint32_t n_ids)
{
    struct seq_pool *pool;
    size_t i;

    ovs_assert(nb_user != 0);
    ovs_assert(base <= UINT32_MAX - n_ids);

    pool = xmalloc(sizeof *pool);

    pool->cache = xcalloc(nb_user, sizeof *pool->cache);
    for (i = 0; i < nb_user; i++) {
        ovs_mutex_init(&pool->cache[i].c_lock);
    }
    pool->nb_user = nb_user;

    pool->next_id = base;
    pool->base = base;
    pool->n_ids = n_ids;

    ovs_mutex_init(&pool->lock);
    ovs_list_init(&pool->free_ids);

    return pool;
}

void
seq_pool_destroy(struct seq_pool *pool)
{
    struct seq_node *node;
    struct seq_node *next;
    size_t i;

    if (!pool) {
        return;
    }

    ovs_mutex_lock(&pool->lock);
    LIST_FOR_EACH_SAFE(node, next, list_node, &pool->free_ids) {
        free(node);
    }
    ovs_list_poison(&pool->free_ids);
    ovs_mutex_unlock(&pool->lock);
    ovs_mutex_destroy(&pool->lock);

    for (i = 0; i < pool->nb_user; i++) {
        ovs_mutex_lock(&pool->cache[i].c_lock);
        pool->cache[i].head = 0;
        pool->cache[i].tail = 0;
        ovs_mutex_unlock(&pool->cache[i].c_lock);
        ovs_mutex_destroy(&pool->cache[i].c_lock);
    }

    free(pool->cache);
    free(pool);
}

bool
seq_pool_new_id(struct seq_pool *pool, unsigned int uid, uint32_t *id)
{
    struct seq_pool_cache *cache;
    struct seq_node *node;
    bool found = false;

    uid %= pool->nb_user;
    cache = &pool->cache[uid];

    ovs_mutex_lock(&cache->c_lock);

    if (!SEQPOOL_C_EMPTY(cache)) {
        *id = SEQPOOL_C_POP(cache);
        found = true;
        goto unlock;
    }

    ovs_mutex_lock(&pool->lock);

    while (!SEQPOOL_C_FULL(cache)
        && !ovs_list_is_empty(&pool->free_ids)) {
        node = CONTAINER_OF(ovs_list_pop_front(&pool->free_ids),
                            struct seq_node, list_node);
        SEQPOOL_C_ADD(cache, node->id);
        free(node);
    }

    while (!SEQPOOL_C_FULL(cache)
        && pool->next_id < pool->base + pool->n_ids) {
        SEQPOOL_C_ADD(cache, pool->next_id++);
    }

    ovs_mutex_unlock(&pool->lock);

    if (SEQPOOL_C_EMPTY(cache)) {
        struct seq_pool_cache *c2;
        size_t i;

        for (i = 0; i < pool->nb_user; i++) {
            if (i == uid) {
                continue;
            }
            c2 = &pool->cache[i];
            /* Danger zone!
             * c2's user could also be attempting to steal from other
             * user's cache at the same time. In such case, it would check
             * 'uid' cache as well, which is locked.
             *
             * Locking c2->c_lock would thus lead to deadlock.
             *
             * First incomplete mitigation is to avoid locking on empty caches.
             */
            if (SEQPOOL_C_EMPTY(c2)) {
                continue;
            }
            /* Nothing prevents 'uid' thread to be suspended here, and for
             * 'c2' user to empty its cache and attempt a new alloc, bringing back
             * the same deadlock as before.
             *
             * So only try to lock the cache lock. This means that in some situation,
             * there will be some IDs remaining in the pool but allocation will fail.
             */
            if (ovs_mutex_trylock(&c2->c_lock)) {
                continue;
            }
            if (!SEQPOOL_C_EMPTY(c2)) {
                SEQPOOL_C_ADD(cache, SEQPOOL_C_POP(c2));
            }
            ovs_mutex_unlock(&c2->c_lock);
        }
    }

    if (!SEQPOOL_C_EMPTY(cache)) {
        *id = SEQPOOL_C_POP(cache);
        found = true;
    }

unlock:
    ovs_mutex_unlock(&cache->c_lock);
    return found;
}

void
seq_pool_free_id(struct seq_pool *pool, unsigned int uid, uint32_t id)
{
    struct seq_node *nodes[SEQPOOL_C_SIZE + 1];
    struct seq_pool_cache *cache;
    uint32_t node_id;
    size_t i;

    if (id < pool->base || id >= pool->base + pool->n_ids)
        return;

    uid %= pool->nb_user;
    cache = &pool->cache[uid];

    ovs_mutex_lock(&cache->c_lock);

    if (!SEQPOOL_C_FULL(cache)) {
        SEQPOOL_C_ADD(cache, id);
        ovs_mutex_unlock(&cache->c_lock);
        return;
    }

    i = 0;
    memset(nodes, 0, sizeof(nodes));

    /* Flush the cache. */
    while (!SEQPOOL_C_EMPTY(cache)) {
        nodes[i] = xmalloc(sizeof **nodes);
        nodes[i]->id = SEQPOOL_C_POP(cache);
        i++;
    }

    /* Finish with the last freed node. */
    nodes[i] = xmalloc(sizeof **nodes);
    nodes[i]->id = id;
    i++;

    ovs_mutex_lock(&pool->lock);
    i = 0;
    while (i < ARRAY_SIZE(nodes) && nodes[i] != NULL) {
        ovs_list_push_back(&pool->free_ids, &nodes[i]->list_node);
        i++;
    }
    ovs_mutex_unlock(&pool->lock);

    ovs_mutex_unlock(&cache->c_lock);
}
