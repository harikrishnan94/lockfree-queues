#ifndef MPMC_QUEUE_HPP
#define MPMC_QUEUE_HPP

#include "cqueue.h"
#include "wait_event/wait_event.h"

#include <functional>
#include <memory>

class mpmc_queue_t
{
private:
	cqueue_mpmc_t *mpmc;
	wait_event_t *producer_wait_event;
	wait_event_t *consumer_wait_event;

public:
	mpmc_queue_t(int queue_size, int max_threads)
	    : mpmc(CreateMPMCQueue(queue_size, max_threads))
	    , producer_wait_event(CreateWaitEvent())
	    , consumer_wait_event(CreateWaitEvent())
	{
	}

	~mpmc_queue_t()
	{
		DestroyMPMCQueue(mpmc);
		DestroyWaitEvent(producer_wait_event);
		DestroyWaitEvent(consumer_wait_event);
	}

	bool
	try_push(void *elem)
	{
		return MPMCQueueTryPush(mpmc, elem);
	}

	bool
	try_pop(void *&elem)
	{
		return MPMCQueueTryPop(mpmc, &elem);
	}

	bool
	is_empty() const
	{
		return MPMCQueueIsEmpty(mpmc);
	}

	bool
	is_full() const
	{
		return MPMCQueueIsFull(mpmc);
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
};

#endif /* MPMC_QUEUE_HPP */
