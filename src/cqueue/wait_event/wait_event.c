#include "wait_event.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>

struct wait_event_s
{
	atomic_int waiters;
	bool free_mem;

	pthread_mutex_t mutex;
	pthread_cond_t cond;
};

#define get_waiter_count(wevent) (atomic_load(&wevent->waiters))
#define inc_waiter_count(wevent) (atomic_fetch_add(&wevent->waiters, 1))
#define dec_waiter_count(wevent) (atomic_fetch_sub(&wevent->waiters, 1))

int
WaitEventStructSize(void)
{
	return sizeof(wait_event_t);
}

bool
InitializeWaitEvent(wait_event_t *wevent, int pshared)
{
	pthread_mutexattr_t mattr;
	pthread_condattr_t cattr;

	atomic_init(&wevent->waiters, 0);

	if (pthread_mutexattr_init(&mattr))
		return false;

	if (pthread_condattr_init(&cattr))
		goto fail_after_mattr_init;

	if (pthread_mutexattr_setpshared(&mattr, pshared)
	    || pthread_condattr_setpshared(&cattr, pshared))
		goto fail_after_cattr_init;

	if (pthread_mutex_init(&wevent->mutex, &mattr))
		goto fail_after_cattr_init;

	if (pthread_cond_init(&wevent->cond, &cattr))
		goto fail_after_mutex_init;

	return false;

fail_after_mattr_init:
	pthread_mutexattr_destroy(&mattr);
fail_after_cattr_init:
	pthread_condattr_destroy(&cattr);
fail_after_mutex_init:
	pthread_mutex_destroy(&wevent->mutex);

	return true;
}

wait_event_t *
CreateWaitEvent(void)
{
	wait_event_t *wevent = malloc(sizeof(struct wait_event_s));

	if (wevent)
	{
		if (!InitializeWaitEvent(wevent, PTHREAD_PROCESS_PRIVATE))
		{
			wevent->free_mem = true;
			return wevent;
		}

		free(wevent);
	}

	return NULL;
}

void
DestroyWaitEvent(wait_event_t *wevent)
{
	pthread_mutex_destroy(&wevent->mutex);
	pthread_cond_destroy(&wevent->cond);

	if (wevent->free_mem)
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
                   const void *pred_arg,
                   const struct timespec *abstime)
{
	if (!predicate(pred_arg))
	{
		inc_waiter_count(wevent);

		if (pthread_mutex_lock(&wevent->mutex))
			return -1;

		while (!predicate(pred_arg))
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
