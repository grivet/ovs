#include <stdint.h>
#include <stdbool.h>

#include "ovs-atomic.h"

/* Bounded lockless queue
 * ======================
 *
 * A lockless FIFO queue bounded to a known size.
 * Each operation (insert, remove) uses one CAS().
 *
 * The structure is:
 *
 *   Multi-producer: multiple threads can write to it
 *   concurrently.
 *
 *   Multi-consumer: multiple threads can read from it
 *   concurrently.
 *
 *   Bounded: the queue is backed by external memory.
 *   No new allocation is made on insertion, only the
 *   used elements in the queue are marked as such.
 *   The boundary of the queue is defined as the size given
 *   at init, which must be a power of two.
 *
 *   Failing: when an operation (enqueue, dequeue) cannot
 *   be performed due to the queue being full/empty, the
 *   operation immediately fails, instead of waiting on
 *   a state change.
 *
 *   Non-intrusive: queue elements are allocated prior to
 *   initialization.  Data is shallow-copied to those
 *   allocated elements.
 *
 * Thread safety
 * =============
 *
 * The queue is thread-safe for MPMC case.
 * No lock is taken by the queue.  The queue guarantees
 * lock-free forward progress for each of its operations.
 *
 */

/* Type passed to and from insertion and deletion functions.
 * To add supported data types, supplement this union.
 */
union llring_data {
    uint32_t u32;
};

/* A queue element.
 * User must allocate an array of such elements, which must
 * have more than 2 elements and should be of a power-of-two
 * size.
 */
struct llring_node {
    atomic_uint32_t seq;
    union llring_data data;
};

/* A ring description.
 * The head and tail of the ring are padded to avoid false-sharing,
 * which improves slightly multi-thread performance, at the cost
 * of some memory.
 */
struct llring {
    PADDED_MEMBERS(CACHE_LINE_SIZE, atomic_uint32_t head;);
    PADDED_MEMBERS(CACHE_LINE_SIZE, atomic_uint32_t tail;);
    struct llring_node *nodes;
    uint32_t mask;
};

/* Initialize a queue.
 * The 'nodes' parameter is set as the backing array for the ring.
 * It should be at least of size 'size'.  The 'size' parameter must
 * be a power-of-two higher than 2.
 * If any parameter is invalid, this function will return -1.
 */
int llring_init(struct llring *r, struct llring_node *nodes, uint32_t size);

/* 'data' is copied to the latest free slot in the queue.
 * A shallow-copy only is used.
 */
bool llring_enqueue(struct llring *r, union llring_data data);

/* The value within the oldest slot taken in the queue is copied
 * to the address pointed by 'data'.
 */
bool llring_dequeue(struct llring *r, union llring_data *data);
