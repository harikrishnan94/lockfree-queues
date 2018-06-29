#ifndef MPSC_SHMEM_QUEUE_HPP
#define MPSC_SHMEM_QUEUE_HPP

#include "cqueue.h"
#include "wait_event/wait_event.h"

#include <functional>
#include <memory>

class mpsc_shemem_queue_t
{
private:
	shmem_mpsc_queue_t *mpsc;
	wait_event_t *producer_wait_event;
	wait_event_t *consumer_wait_event;

public:
	mpsc_shemem_queue_t(int queue_size, int max_threads)
	    : mpsc(CreateMPSCQueue(queue_size, max_threads))
	    , producer_wait_event(CreateWaitEvent())
	    , consumer_wait_event(CreateWaitEvent())
	{
	}

	~mpsc_shemem_queue_t()
	{
		DestroyMPSCQueue(mpsc);
		DestroyWaitEvent(producer_wait_event);
		DestroyWaitEvent(consumer_wait_event);
	}

	bool
	try_push(void *elem)
	{
		return MPSCQueueTryPush(mpsc, get_tid(), &elem, sizeof(uintptr_t));
	}

	bool
	try_pop(void *&elem)
	{
		return MPSCQueuePop(mpsc, static_cast<void *>(&elem));
	}

	bool
	is_empty() const
	{
		return MPSCQueueIsEmpty(mpsc);
	}

	bool
	is_full() const
	{
		return MPSCQueueIsFull(mpsc, sizeof(uintptr_t));
	}

	int
	producer_wait(const std::function<bool()> &pred, const struct timespec *abstime = NULL)
	{
		return WaitEventTimedWait(producer_wait_event,
		                          predicate_apply,
		                          static_cast<const void *>(&pred),
		                          abstime);
	}

	int
	consumer_wait(const std::function<bool()> &pred, const struct timespec *abstime = NULL)
	{
		return WaitEventTimedWait(consumer_wait_event,
		                          predicate_apply,
		                          static_cast<const void *>(&pred),
		                          abstime);
	}

	int
	wakeup_one_producer()
	{
		return WaitEventWakeupOneWaiter(producer_wait_event);
	}

	int
	wakeup_one_consumer()
	{
		return WaitEventWakeupOneWaiter(consumer_wait_event);
	}

	int
	wakeup_all_producers()
	{
		return WaitEventWakeupAllWaiters(producer_wait_event);
	}

	int
	wakeup_all_consumers()
	{
		return WaitEventWakeupAllWaiters(consumer_wait_event);
	}

private:
	static bool
	predicate_apply(const void *arg)
	{
		auto &predicate = *static_cast<const std::function<int()> *>(arg);

		return predicate();
	}

	static int
	get_tid()
	{
		static std::atomic_int tid_gen = 0;
		static thread_local int __tid  = -1;

		if (__tid == -1)
			__tid = tid_gen.fetch_add(1);

		return __tid;
	}
};

#endif /* MPSC_SHMEM_QUEUE_HPP */
