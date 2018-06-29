#include "cqueue.h"
#include "wait_event/wait_event.h"

#include <functional>
#include <memory>

class cqueue_t
{
private:
	cqueue_mpsc_t *mpsc;
	wait_event_t *producer_wait_event;
	wait_event_t *consumer_wait_event;

public:
	cqueue_t(int queue_size, int max_threads)
	    : mpsc(CreateMPSCQueue(queue_size, max_threads))
	    , producer_wait_event(CreateWaitEvent())
	    , consumer_wait_event(CreateWaitEvent())
	{
	}

	~cqueue_t()
	{
		DestroyMPSCQueue(mpsc);
		DestroyWaitEvent(producer_wait_event);
		DestroyWaitEvent(consumer_wait_event);
	}

	bool
	try_push(void *elem)
	{
		return MPSCQueueTryPush(mpsc, elem);
	}

	bool
	try_pop(void *&elem)
	{
		return MPSCQueueTryPop(mpsc, &elem);
	}

	bool
	is_empty() const
	{
		return MPSCQueueIsEmpty(mpsc);
	}

	bool
	is_full() const
	{
		return MPSCQueueIsFull(mpsc);
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
