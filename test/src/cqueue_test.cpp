#include "catch.hpp"

#include "cqueue.h"

#define as_ptr(x) (reinterpret_cast<void *>(x))

TEST_CASE("mpmc", "[cqueue]")
{
	cqueue_mpmc_t *mpmc          = CreateMPMCQueue(3, 1);
	std::vector<void *> pointers = { as_ptr(0x00), as_ptr(0x02), as_ptr(0x04) };
	void *elem;

	REQUIRE(MPMCQueueTryPop(mpmc, &elem) == false);

	REQUIRE(MPMCQueueTryPush(mpmc, as_ptr(0x00)) == true);
	REQUIRE(MPMCQueueTryPush(mpmc, as_ptr(0x02)) == true);
	REQUIRE(MPMCQueueTryPush(mpmc, as_ptr(0x04)) == true);

	REQUIRE(MPMCQueueTryPush(mpmc, as_ptr(0x06)) == false);

	elem = NULL;
	REQUIRE(MPMCQueueTryPop(mpmc, &elem) == true);
	REQUIRE(elem == as_ptr(0x00));

	REQUIRE(MPMCQueueTryPush(mpmc, as_ptr(0x06)) == true);

	elem = NULL;
	REQUIRE(MPMCQueueTryPop(mpmc, &elem) == true);
	REQUIRE(elem == as_ptr(0x02));

	elem = NULL;
	REQUIRE(MPMCQueueTryPop(mpmc, &elem) == true);
	REQUIRE(elem == as_ptr(0x04));

	elem = NULL;
	REQUIRE(MPMCQueueTryPop(mpmc, &elem) == true);
	REQUIRE(elem == as_ptr(0x06));

	DestroyMPMCQueue(mpmc);
}

TEST_CASE("mpsc", "[cqueue]")
{
	shmem_mpsc_queue_t *mpsc     = CreateMPSCQueue(3 * (sizeof(uintptr_t) + sizeof(int)), 1);
	std::vector<void *> pointers = { as_ptr(0x00), as_ptr(0x02), as_ptr(0x04) };
	void *elem;

	REQUIRE(MPSCQueuePop(mpsc, &elem) == false);

	elem = as_ptr(0x00);
	REQUIRE(MPSCQueueTryPush(mpsc, 0, &elem, sizeof(uintptr_t)) == true);
	elem = as_ptr(0x02);
	REQUIRE(MPSCQueueTryPush(mpsc, 0, &elem, sizeof(uintptr_t)) == true);
	elem = as_ptr(0x04);
	REQUIRE(MPSCQueueTryPush(mpsc, 0, &elem, sizeof(uintptr_t)) == true);

	REQUIRE(MPSCQueueTryPush(mpsc, 0, &elem, sizeof(uintptr_t)) == false);

	elem = NULL;
	REQUIRE(MPSCQueuePop(mpsc, &elem) == true);
	REQUIRE(elem == as_ptr(0x00));

	elem = as_ptr(0x06);
	REQUIRE(MPSCQueueTryPush(mpsc, 0, &elem, sizeof(uintptr_t)) == true);

	elem = NULL;
	REQUIRE(MPSCQueuePop(mpsc, &elem) == true);
	REQUIRE(elem == as_ptr(0x02));

	elem = NULL;
	REQUIRE(MPSCQueuePop(mpsc, &elem) == true);
	REQUIRE(elem == as_ptr(0x04));

	elem = NULL;
	REQUIRE(MPSCQueuePop(mpsc, &elem) == true);
	REQUIRE(elem == as_ptr(0x06));

	DestroyMPSCQueue(mpsc);
}
