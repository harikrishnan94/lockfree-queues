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

#define __CACHE_ALIGN__ _Alignas(64)

struct thread_pos_t
{
	__CACHE_ALIGN__ atomic_size_t head;
};

struct shmem_mpsc_queue_s
{
	void *queue_data;
	size_t queue_size;
	bool free_mem;
	int max_producers;

	__CACHE_ALIGN__ atomic_size_t head;
	__CACHE_ALIGN__ atomic_size_t tail;

	__CACHE_ALIGN__ atomic_size_t last_head;

	struct thread_pos_t *tpos;
};

#define STORE_RELEASE(aptr, val) atomic_store_explicit((aptr), (val), memory_order_release)
#define STORE_RELAXED(aptr, val) atomic_store_explicit((aptr), (val), memory_order_relaxed)
#define LOAD_ACQUIRE(aptr) atomic_load_explicit((aptr), memory_order_acquire)
#define LOAD_RELAXED(aptr) atomic_load_explicit((aptr), memory_order_relaxed)

#define SET_HEAD_POS(mpsc, producer_id, _head) \
	STORE_RELEASE(&(mpsc)->tpos[producer_id].head, (_head))

static int get_next_elem_size(shmem_mpsc_queue_t *mpsc, bool try_again);
static bool queue_is_empty(size_t head, size_t tail);
static bool queue_is_full(size_t head, size_t tail, int elem_size, size_t max);
static void copy_to_queue(shmem_mpsc_queue_t *mpsc, size_t pos, void *elem, int elem_size);
static void copy_from_queue(shmem_mpsc_queue_t *mpsc, size_t pos, void *elem, int *elem_size);

size_t
CalculateMPSCQueueSize(size_t queue_size, int max_producers)
{
	size_t size = queue_size;

	if (queue_size <= sizeof(int))
		return 0;

	size += sizeof(shmem_mpsc_queue_t);
	size += sizeof(struct thread_pos_t) * max_producers;

	return size;
}

void
InitializeMPSCQueue(shmem_mpsc_queue_t *mpsc, size_t queue_size, int max_producers)
{
	char *p = (char *) mpsc;

	p += sizeof(shmem_mpsc_queue_t);
	mpsc->tpos = (struct thread_pos_t *) p;

	p += sizeof(struct thread_pos_t) * max_producers;
	mpsc->queue_data = p;

	mpsc->queue_size    = queue_size;
	mpsc->max_producers = max_producers;

	atomic_init(&mpsc->head, 0);
	atomic_init(&mpsc->tail, 0);
	atomic_init(&mpsc->last_head, 0);

	for (int i = 0; i < max_producers; i++)
		mpsc->tpos[i].head = SIZE_T_MAX;
}

shmem_mpsc_queue_t *
CreateMPSCQueue(size_t queue_size, int max_producers)
{
	size_t size              = CalculateMPSCQueueSize(queue_size, max_producers);
	shmem_mpsc_queue_t *mpsc = malloc(size);

	if (mpsc)
	{
		InitializeMPSCQueue(mpsc, queue_size, max_producers);
		mpsc->free_mem = true;
	}

	return mpsc;
}

void
DestroyMPSCQueue(shmem_mpsc_queue_t *mpsc)
{
	free(mpsc);
}

static void
update_last_head(shmem_mpsc_queue_t *mpsc)
{
	size_t last_head = LOAD_ACQUIRE(&mpsc->head);

	for (int i = 0; i < mpsc->max_producers; i++)
	{
		size_t head = LOAD_ACQUIRE(&mpsc->tpos[i].head);

		if (head < last_head)
			last_head = head;
	}

	STORE_RELEASE(&mpsc->last_head, last_head);
}

static size_t
reserve_head_to_produce(shmem_mpsc_queue_t *mpsc, int producer_id, int elem_size)
{
	size_t queue_size = mpsc->queue_size;
	size_t head       = LOAD_ACQUIRE(&mpsc->head);
	size_t tail       = LOAD_ACQUIRE(&mpsc->tail);
	backoff_t boff;

	backoff_init(&boff, 10, 1000);

	while (!queue_is_full(head, tail, elem_size, queue_size))
	{
		SET_HEAD_POS(mpsc, producer_id, head);

		if (atomic_compare_exchange_strong(&mpsc->head, &head, head + elem_size))
			return head;

		usleep(backoff_do_eb(&boff));
	}

	return SIZE_T_MAX;
}

bool
MPSCQueueTryPush(shmem_mpsc_queue_t *mpsc, int producer_id, void *elem, int elem_size)
{
	size_t head   = reserve_head_to_produce(mpsc, producer_id, elem_size + sizeof(int));
	bool did_push = false;

	if (head != SIZE_T_MAX)
	{
		did_push = true;
		copy_to_queue(mpsc, head, elem, elem_size);
	}

	SET_HEAD_POS(mpsc, producer_id, SIZE_T_MAX);

	return did_push;
}

static int
get_next_elem_size(shmem_mpsc_queue_t *mpsc, bool try_again)
{
	size_t head = LOAD_ACQUIRE(&mpsc->last_head);
	size_t tail = LOAD_ACQUIRE(&mpsc->tail);

	if (!queue_is_empty(head, tail))
		return *(int *) ((char *) mpsc->queue_data + tail % mpsc->queue_size);

	if (try_again)
		update_last_head(mpsc);

	return try_again ? get_next_elem_size(mpsc, false) : -1;
}

int
MPSCQueueNextElemSize(shmem_mpsc_queue_t *mpsc)
{
	return get_next_elem_size(mpsc, true);
}

bool
MPSCQueuePop(shmem_mpsc_queue_t *mpsc, void *elem)
{
	size_t tail = LOAD_ACQUIRE(&mpsc->tail);
	int elem_size;

	if (get_next_elem_size(mpsc, true) <= 0)
		return false;

	copy_from_queue(mpsc, tail, elem, &elem_size);
	STORE_RELEASE(&mpsc->tail, tail + elem_size + sizeof(int));

	return true;
}

bool
MPSCQueueIsEmpty(shmem_mpsc_queue_t *mpsc)
{
	if (queue_is_empty(LOAD_ACQUIRE(&mpsc->last_head), LOAD_ACQUIRE(&mpsc->tail)))
		update_last_head(mpsc);

	return queue_is_empty(LOAD_ACQUIRE(&mpsc->last_head), LOAD_ACQUIRE(&mpsc->tail));
}

bool
MPSCQueueIsFull(shmem_mpsc_queue_t *mpsc, int elem_size)
{
	return queue_is_full(LOAD_ACQUIRE(&mpsc->last_head),
	                     LOAD_ACQUIRE(&mpsc->tail),
	                     elem_size + sizeof(int),
	                     mpsc->queue_size);
}

static bool
queue_is_empty(size_t head, size_t tail)
{
	return tail >= head;
}

static bool
queue_is_full(size_t head, size_t tail, int elem_size, size_t max)
{
	return head + elem_size - 1 >= tail + max;
}

static void
copy_to_queue(shmem_mpsc_queue_t *mpsc, size_t pos, void *elem, int elem_size)
{
	char *queue_data  = mpsc->queue_data;
	size_t queue_size = mpsc->queue_size;

	*(int *) (queue_data + pos % queue_size) = elem_size;

	pos += sizeof(int);

	for (int i = 0; i < elem_size; i++)
		queue_data[(i + pos) % queue_size] = ((char *) elem)[i];
}

static void
copy_from_queue(shmem_mpsc_queue_t *mpsc, size_t pos, void *elem, int *elem_size)
{
	char *queue_data  = mpsc->queue_data;
	size_t queue_size = mpsc->queue_size;

	*elem_size = *(int *) (queue_data + pos % queue_size);

	pos += sizeof(int);

	for (int i = 0; i < *elem_size; i++)
		((char *) elem)[i] = queue_data[(i + pos) % queue_size];
}
