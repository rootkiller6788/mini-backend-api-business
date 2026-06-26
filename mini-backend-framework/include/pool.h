#ifndef POOL_H
#define POOL_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <time.h>

/*
 * L1: Core Definitions ！ Object / Connection Pool
 *
 * An object pool manages a fixed set of reusable resources (connections,
 * threads, buffers). It avoids the overhead of repeated allocation/
 * deallocation by recycling objects.
 *
 * L3: Engineering Structure ！ bounded semaphore + free list.
 *   - acquire(): take from free list, block/wait if empty
 *   - release(): return to free list, signal waiters
 *   - idle timeout: auto-close connections idle too long
 *
 * L6: Classic Problem ！ database connection pooling is a canonical
 * system design problem (see HikariCP, Apache DBCP). Core tradeoff:
 * pool size vs. database connection limits vs. application threads.
 *
 * L4: Little's Law applied to pools:
 *   L = lambda * W  where L = connections in use, lambda = request rate,
 *   W = average connection hold time. Pool size must >= L for stability.
 */

#define POOL_MAX_RESOURCES  128
#define POOL_MAX_NAME       64
#define POOL_MAX_URL        256

/* Forward declaration for user-defined resource */
typedef void *(*PoolCreator)(const char *url, void *arg);
typedef void  (*PoolDestroyer)(void *resource);
typedef bool  (*PoolValidator)(void *resource);

typedef struct {
    void    *resource;
    bool     in_use;
    struct timespec last_used;
    int      use_count;
} PoolSlot;

typedef struct {
    PoolSlot      slots[POOL_MAX_RESOURCES];
    int           capacity;           /* max pool size */
    int           in_use;             /* currently checked out */
    int           idle_count;         /* idle and available */
    char          url[POOL_MAX_URL];  /* connection URL / config */
    PoolCreator   create_fn;
    PoolDestroyer destroy_fn;
    PoolValidator validate_fn;
    void         *create_arg;         /* user arg for create_fn */
    int           idle_timeout_sec;   /* 0 = no timeout */
    int64_t       total_acquired;     /* stats */
    int64_t       total_released;
    int64_t       total_timeouts;
    int64_t       total_failures;
    bool          initialized;
} Pool;

/* Initialize pool with creator/destroyer/validator callbacks.
 * capacity: max concurrent resources. idle_timeout: seconds before
 * idle resources are considered stale (0 = no timeout). */
void pool_init(Pool *pool, int capacity, int idle_timeout_sec,
               const char *url, PoolCreator create_fn,
               PoolDestroyer destroy_fn, PoolValidator validate_fn,
               void *create_arg);

/* Acquire a resource. Returns NULL if no resource available within
 * timeout_ms milliseconds. If timeout_ms is -1, block indefinitely.
 * Complexity: O(n) scan of slots. */
void *pool_acquire(Pool *pool, int timeout_ms);

/* Release a resource back to the pool.
 * Complexity: O(n) scan to find slot. */
int  pool_release(Pool *pool, void *resource);

/* L3: Lazy sweep ！ close idle connections that have been idle
 * longer than idle_timeout_sec. Returns count of closed resources. */
int  pool_sweep_idle(Pool *pool);

/* Get pool stats */
int  pool_available(const Pool *pool);
int  pool_in_use(const Pool *pool);
int  pool_capacity(const Pool *pool);

/* Close all resources and reset pool */
void pool_destroy(Pool *pool);

/* Pre-warm pool by creating min_idle resources */
int  pool_prewarm(Pool *pool, int min_idle);

/*
 * L7: Application-level helpers for common pool types
 */

/* Pre-built creator for TCP-like resources (fd-based) */
typedef void *(*PoolFdCreator)(const char *url, int *fd_out);

#endif
