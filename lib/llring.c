#include <config.h>

#include "ovs-atomic.h"

#include "llring.h"

int
llring_init(struct llring *r, struct llring_node *nodes, uint32_t size)
{
    uint32_t i;

    r->nodes = nodes;

    if (size < 2 || !IS_POW2(size)) {
        return -1;
    }

    r->mask = size - 1;

    for (i = 0; i < size; i++) {
        atomic_store_relaxed(&nodes[i].seq, i);
    }
    atomic_store_relaxed(&r->head, 0);
    atomic_store_relaxed(&r->tail, 0);

    return 0;
}

bool
llring_enqueue(struct llring *r, union llring_data data)
{
    struct llring_node *node;
    uint32_t pos;

    atomic_read_relaxed(&r->head, &pos);
    while (true) {
        int64_t diff;
        uint32_t seq;

        node = &r->nodes[pos & r->mask];
        atomic_read_explicit(&node->seq, &seq, memory_order_acquire);
        diff = (int64_t)seq - (int64_t)pos;

        if (diff == 0) {
            if (atomic_compare_exchange_weak_explicit(&r->head, &pos, pos + 1,
                                 memory_order_relaxed, memory_order_relaxed)) {
                break;
            }
        } else if (diff < 0) {
            return false;
        } else {
            atomic_read_relaxed(&r->head, &pos);
        }
    }

    node->data = data;
    atomic_store_explicit(&node->seq, pos + 1, memory_order_release);
    return true;
}

bool
llring_dequeue(struct llring *r, union llring_data *data)
{
    struct llring_node *node;
    uint32_t pos;

    atomic_read_relaxed(&r->tail, &pos);
    while (true) {
        int64_t diff;
        uint32_t seq;

        node = &r->nodes[pos & r->mask];
        atomic_read_explicit(&node->seq, &seq, memory_order_acquire);
        diff = (int64_t)seq - (int64_t)(pos + 1);

        if (diff == 0) {
            if (atomic_compare_exchange_weak_explicit(&r->tail, &pos, pos + 1,
                                 memory_order_relaxed, memory_order_relaxed)) {
                break;
            }
        } else if (diff < 0) {
            return false;
        } else {
            atomic_read_relaxed(&r->tail, &pos);
        }
    }

    *data = node->data;
    atomic_store_explicit(&node->seq, pos + r->mask + 1, memory_order_release);
    return true;
}
