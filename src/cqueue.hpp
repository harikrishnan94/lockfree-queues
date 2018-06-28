#include "cqueue.h"

#include <memory>

class cqueue_t
{
private:
	cqueue_mpsc_t *mpsc;

public:
	cqueue_t(int queue_size, int max_threads) : mpsc(CreateMPSCQueue(queue_size, max_threads)) {}
	~cqueue_t() { DestroyMPSCQueue(mpsc); }

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
	is_empty()
	{
		return MPSCQueueIsEmpty(mpsc);
	}

	bool
	is_full()
	{
		return MPSCQueueIsFull(mpsc);
	}
};
