#include "backoff/backoff.h"
#include "cqueue.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef _Atomic(void *) atomic_pointer;

#define __CACHE_ALIGN__ _Alignas(64)

struct thread_pos_t
{
	__CACHE_ALIGN__ atomic_long head;
	atomic_long tail;
};

struct cqueue_mpmc_s
{
	atomic_pointer *queue_data;
	int queue_size;
	int max_threads;

	__CACHE_ALIGN__ atomic_long head;
	__CACHE_ALIGN__ atomic_long tail;

	__CACHE_ALIGN__ atomic_long last_head;
	__CACHE_ALIGN__ atomic_long last_tail;

	struct thread_pos_t *tpos;
};

#define STORE_RELEASE(aptr, val) atomic_store_explicit((aptr), (val), memory_order_release)
#define STORE_RELAXED(aptr, val) atomic_store_explicit((aptr), (val), memory_order_relaxed)
#define LOAD_ACQUIRE(aptr) atomic_load_explicit((aptr), memory_order_acquire)
#define LOAD_RELAXED(aptr) atomic_load_explicit((aptr), memory_order_relaxed)

#define SET_HEAD_POS(mpmc, _head) STORE_RELEASE(&(mpmc)->tpos[get_tid()].head, (_head))
#define SET_TAIL_POS(mpmc, _tail) STORE_RELEASE(&(mpmc)->tpos[get_tid()].tail, (_tail))

static int get_tid(void);
static bool queue_is_empty(long head, long tail);
static bool queue_is_full(long head, long tail, long max);

cqueue_mpmc_t *
CreateMPMCQueue(int queue_size, int max_threads)
{
	cqueue_mpmc_t *mpmc = malloc(sizeof(struct cqueue_mpmc_s));

	mpmc->queue_data  = malloc(sizeof(atomic_pointer) * queue_size);
	mpmc->queue_size  = queue_size;
	mpmc->max_threads = max_threads;
	mpmc->tpos        = malloc(sizeof(struct thread_pos_t) * max_threads);

	atomic_init(&mpmc->head, 0);
	atomic_init(&mpmc->tail, 0);
	atomic_init(&mpmc->last_head, 0);
	atomic_init(&mpmc->last_tail, 0);

	for (int i = 0; i < max_threads; i++)
	{
		mpmc->tpos[i].head = LONG_MAX;
		mpmc->tpos[i].tail = LONG_MAX;
	}

	return mpmc;
}

void
DestroyMPMCQueue(cqueue_mpmc_t *mpmc)
{
	free(mpmc->queue_data);
	free(mpmc);
}

static void
update_last_tail(cqueue_mpmc_t *mpmc)
{
	long last_tail = LOAD_ACQUIRE(&mpmc->tail);

	for (int i = 0; i < mpmc->max_threads; i++)
	{
		long tail = LOAD_ACQUIRE(&mpmc->tpos[i].tail);

		if (tail < last_tail)
			last_tail = tail;
	}

	STORE_RELEASE(&mpmc->last_tail, last_tail);
}

static void
update_last_head(cqueue_mpmc_t *mpmc)
{
	long last_head = LOAD_ACQUIRE(&mpmc->head);

	for (int i = 0; i < mpmc->max_threads; i++)
	{
		long head = LOAD_ACQUIRE(&mpmc->tpos[i].head);

		if (head < last_head)
			last_head = head;
	}

	STORE_RELEASE(&mpmc->last_head, last_head);
}

static long
reserve_head_to_produce(cqueue_mpmc_t *mpmc, bool try_again)
{
	int queue_size = mpmc->queue_size;
	long head      = LOAD_ACQUIRE(&mpmc->head);
	long last_tail = LOAD_ACQUIRE(&mpmc->last_tail);
	backoff_t boff;

	backoff_init(&boff, 10, 1000);

	while (!queue_is_full(head, last_tail, queue_size))
	{
		SET_HEAD_POS(mpmc, head);

		if (atomic_compare_exchange_strong(&mpmc->head, &head, head + 1))
			return head;

		usleep(backoff_do_eb(&boff));
	}

	if (try_again)
		update_last_tail(mpmc);

	return try_again ? reserve_head_to_produce(mpmc, false) : -1;
}

static long
reserve_tail_to_consume(cqueue_mpmc_t *mpmc, bool try_again)
{
	long last_head = LOAD_ACQUIRE(&mpmc->last_head);
	long tail      = LOAD_ACQUIRE(&mpmc->tail);
	backoff_t boff;

	backoff_init(&boff, 10, 1000);

	while (!queue_is_empty(last_head, tail))
	{
		SET_TAIL_POS(mpmc, tail);

		if (atomic_compare_exchange_strong(&mpmc->tail, &tail, tail + 1))
			return tail;

		usleep(backoff_do_eb(&boff));
	}

	if (try_again)
		update_last_head(mpmc);

	return try_again ? reserve_tail_to_consume(mpmc, false) : -1;
}

bool
MPMCQueueTryPush(cqueue_mpmc_t *mpmc, void *elem)
{
	long head     = reserve_head_to_produce(mpmc, true);
	bool did_push = false;

	if (head >= 0)
	{
		did_push = true;
		STORE_RELAXED(mpmc->queue_data + head % mpmc->queue_size, elem);
	}

	SET_HEAD_POS(mpmc, LONG_MAX);

	return did_push;
}

bool
MPMCQueueTryPop(cqueue_mpmc_t *mpmc, void **elem_out)
{
	long tail    = reserve_tail_to_consume(mpmc, true);
	bool did_pop = false;

	if (tail >= 0)
	{
		did_pop   = true;
		*elem_out = LOAD_RELAXED(mpmc->queue_data + tail % mpmc->queue_size);
	}

	SET_TAIL_POS(mpmc, LONG_MAX);

	return did_pop;
}

bool
MPMCQueueIsEmpty(cqueue_mpmc_t *mpmc)
{
	if (queue_is_empty(LOAD_ACQUIRE(&mpmc->last_head), LOAD_ACQUIRE(&mpmc->tail)))
		update_last_head(mpmc);
	else
		return false;

	return queue_is_empty(LOAD_ACQUIRE(&mpmc->last_head), LOAD_ACQUIRE(&mpmc->tail));
}

bool
MPMCQueueIsFull(cqueue_mpmc_t *mpmc)
{
	if (queue_is_full(LOAD_ACQUIRE(&mpmc->head), LOAD_ACQUIRE(&mpmc->last_tail), mpmc->queue_size))
		update_last_tail(mpmc);
	else
		return false;

	return queue_is_full(LOAD_ACQUIRE(&mpmc->head),
	                     LOAD_ACQUIRE(&mpmc->last_tail),
	                     mpmc->queue_size);
}

static bool
queue_is_empty(long head, long tail)
{
	return tail >= head;
}

static bool
queue_is_full(long head, long tail, long max)
{
	return head >= tail + max;
}

static int
get_tid(void)
{
	static atomic_int tid_gen      = 0;
	static _Thread_local int __tid = -1;

	if (__tid == -1)
		__tid = atomic_fetch_add(&tid_gen, 1);

	return __tid;
}
