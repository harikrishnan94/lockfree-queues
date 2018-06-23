#include "wait_event.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>

struct wait_event_s
{
	atomic_int waiters;

	pthread_mutex_t mutex;
	pthread_cond_t cond;
};

#define get_waiter_count(wevent) (atomic_load(&wevent->waiters))
#define inc_waiter_count(wevent) (atomic_fetch_add(&wevent->waiters, 1))
#define dec_waiter_count(wevent) (atomic_fetch_sub(&wevent->waiters, 1))

wait_event_t *
CreateWaitEvent(void)
{
	wait_event_t *wevent = malloc(sizeof(struct wait_event_s));

	atomic_init(&wevent->waiters, 0);

	if (pthread_mutex_init(&wevent->mutex, NULL))
		goto fail_after_alloc;

	if (pthread_cond_init(&wevent->cond, NULL))
		goto fail_after_mutex_init;

	return wevent;

fail_after_alloc:
	free(wevent);
fail_after_mutex_init:
	pthread_mutex_destroy(&wevent->mutex);

	return NULL;
}

void
DestroyWaitEvent(wait_event_t *wevent)
{
	pthread_mutex_destroy(&wevent->mutex);
	pthread_cond_destroy(&wevent->cond);
	free(wevent);
}

int
WaitEventNumWaiters(wait_event_t *wevent)
{
	return get_waiter_count(wevent);
}

int
WaitEventTimedWait(wait_event_t *wevent,
                   wait_event_predicate_t predicate,
                   void *pred_arg,
                   const struct timespec *abstime)
{
	if (predicate(pred_arg))
	{
		inc_waiter_count(wevent);

		if (pthread_mutex_lock(&wevent->mutex))
			return -1;

		while (predicate(pred_arg))
		{
			if (pthread_cond_timedwait(&wevent->cond, &wevent->mutex, abstime))
				return -1;
		}

		dec_waiter_count(wevent);

		if (pthread_mutex_unlock(&wevent->mutex))
			return -1;

		return 1;
	}

	return 0;
}

int
WaitEventWakeupOneWaiter(wait_event_t *wevent)
{
	if (get_waiter_count(wevent))
	{
		if (pthread_mutex_lock(&wevent->mutex))
			return -1;

		if (pthread_cond_signal(&wevent->cond))
			return -1;

		if (pthread_mutex_unlock(&wevent->mutex))
			return -1;

		return 1;
	}

	return 0;
}

int
WaitEventWakeupAllWaiters(wait_event_t *wevent)
{
	if (get_waiter_count(wevent))
	{
		if (pthread_mutex_lock(&wevent->mutex))
			return -1;

		if (pthread_cond_broadcast(&wevent->cond))
			return -1;

		if (pthread_mutex_unlock(&wevent->mutex))
			return -1;

		return 1;
	}

	return 0;
}
