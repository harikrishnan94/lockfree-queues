#include "catch.hpp"

#include "cqueue.h"

#define as_ptr(x) (reinterpret_cast<void *>(x))

TEST_CASE("Basic Test", "[cqueue]")
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
