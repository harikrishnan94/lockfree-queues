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

struct cqueue_mpsc_s
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

#define SET_HEAD_POS(mpsc, _head) STORE_RELEASE(&(mpsc)->tpos[get_tid()].head, (_head))
#define SET_TAIL_POS(mpsc, _tail) STORE_RELEASE(&(mpsc)->tpos[get_tid()].tail, (_tail))

static int get_tid(void);
static bool queue_is_empty(long head, long tail);
static bool queue_is_full(long head, long tail, long max);

cqueue_mpsc_t *
CreateMPSCQueue(int queue_size, int max_threads)
{
	cqueue_mpsc_t *mpsc = malloc(sizeof(struct cqueue_mpsc_s));

	mpsc->queue_data  = malloc(sizeof(atomic_pointer) * queue_size);
	mpsc->queue_size  = queue_size;
	mpsc->max_threads = max_threads;
	mpsc->tpos        = malloc(sizeof(struct thread_pos_t) * max_threads);

	atomic_init(&mpsc->head, 0);
	atomic_init(&mpsc->tail, 0);
	atomic_init(&mpsc->last_head, 0);
	atomic_init(&mpsc->last_tail, 0);

	for (int i = 0; i < max_threads; i++)
	{
		mpsc->tpos[i].head = LONG_MAX;
		mpsc->tpos[i].tail = LONG_MAX;
	}

	return mpsc;
}

void
DestroyMPSCQueue(cqueue_mpsc_t *mpsc)
{
	free(mpsc->queue_data);
	free(mpsc);
}

static void
update_last_tail(cqueue_mpsc_t *mpsc)
{
	long last_tail = LOAD_ACQUIRE(&mpsc->tail);

	for (int i = 0; i < mpsc->max_threads; i++)
	{
		long tail = LOAD_ACQUIRE(&mpsc->tpos[i].tail);

		if (tail < last_tail)
			last_tail = tail;
	}

	STORE_RELEASE(&mpsc->last_tail, last_tail);
}

static void
update_last_head(cqueue_mpsc_t *mpsc)
{
	long last_head = LOAD_ACQUIRE(&mpsc->head);

	for (int i = 0; i < mpsc->max_threads; i++)
	{
		long head = LOAD_ACQUIRE(&mpsc->tpos[i].head);

		if (head < last_head)
			last_head = head;
	}

	STORE_RELEASE(&mpsc->last_head, last_head);
}

static long
reserve_head_to_produce(cqueue_mpsc_t *mpsc, bool try_again)
{
	int queue_size = mpsc->queue_size;
	long head      = LOAD_ACQUIRE(&mpsc->head);
	long last_tail = LOAD_ACQUIRE(&mpsc->last_tail);
	backoff_t boff;

	backoff_init(&boff, 10, 1000);

	while (!queue_is_full(head, last_tail, queue_size))
	{
		SET_HEAD_POS(mpsc, head);

		if (atomic_compare_exchange_strong(&mpsc->head, &head, head + 1))
			return head;

		usleep(backoff_do_eb(&boff));
	}

	if (try_again)
		update_last_tail(mpsc);

	return try_again ? reserve_head_to_produce(mpsc, false) : -1;
}

static long
reserve_tail_to_consume(cqueue_mpsc_t *mpsc, bool try_again)
{
	long last_head = LOAD_ACQUIRE(&mpsc->last_head);
	long tail      = LOAD_ACQUIRE(&mpsc->tail);
	backoff_t boff;

	backoff_init(&boff, 10, 1000);

	while (!queue_is_empty(last_head, tail))
	{
		SET_TAIL_POS(mpsc, tail);

		if (atomic_compare_exchange_strong(&mpsc->tail, &tail, tail + 1))
			return tail;

		usleep(backoff_do_eb(&boff));
	}

	if (try_again)
		update_last_head(mpsc);

	return try_again ? reserve_tail_to_consume(mpsc, false) : -1;
}

bool
MPSCQueueTryPush(cqueue_mpsc_t *mpsc, void *elem)
{
	long head     = reserve_head_to_produce(mpsc, true);
	bool did_push = false;

	if (head >= 0)
	{
		did_push = true;
		STORE_RELAXED(mpsc->queue_data + head % mpsc->queue_size, elem);
	}

	SET_HEAD_POS(mpsc, LONG_MAX);

	return did_push;
}

bool
MPSCQueueTryPop(cqueue_mpsc_t *mpsc, void **elem_out)
{
	long tail    = reserve_tail_to_consume(mpsc, true);
	bool did_pop = false;

	if (tail >= 0)
	{
		did_pop   = true;
		*elem_out = LOAD_RELAXED(mpsc->queue_data + tail % mpsc->queue_size);
	}

	SET_TAIL_POS(mpsc, LONG_MAX);

	return did_pop;
}

bool
MPSCQueueIsEmpty(cqueue_mpsc_t *mpsc)
{
	if (queue_is_empty(LOAD_ACQUIRE(&mpsc->last_head), LOAD_ACQUIRE(&mpsc->tail)))
		update_last_head(mpsc);
	else
		return false;

	return queue_is_empty(LOAD_ACQUIRE(&mpsc->last_head), LOAD_ACQUIRE(&mpsc->tail));
}

bool
MPSCQueueIsFull(cqueue_mpsc_t *mpsc)
{
	if (queue_is_full(LOAD_ACQUIRE(&mpsc->head), LOAD_ACQUIRE(&mpsc->last_tail), mpsc->queue_size))
		update_last_tail(mpsc);
	else
		return false;

	return queue_is_full(LOAD_ACQUIRE(&mpsc->head),
	                     LOAD_ACQUIRE(&mpsc->last_tail),
	                     mpsc->queue_size);
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
