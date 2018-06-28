#include "catch.hpp"

#include "cqueue.h"

#define as_ptr(x) (reinterpret_cast<void *>(x))

TEST_CASE("Basic Test", "[cqueue]")
{
	cqueue_mpsc_t *mpsc          = CreateMPSCQueue(3, 1);
	std::vector<void *> pointers = { as_ptr(0x00), as_ptr(0x02), as_ptr(0x04) };
	void *elem;

	REQUIRE(MPSCQueueTryPop(mpsc, &elem) == false);

	REQUIRE(MPSCQueueTryPush(mpsc, as_ptr(0x00)) == true);
	REQUIRE(MPSCQueueTryPush(mpsc, as_ptr(0x02)) == true);
	REQUIRE(MPSCQueueTryPush(mpsc, as_ptr(0x04)) == true);

	REQUIRE(MPSCQueueTryPush(mpsc, as_ptr(0x06)) == false);

	elem = NULL;
	REQUIRE(MPSCQueueTryPop(mpsc, &elem) == true);
	REQUIRE(elem == as_ptr(0x00));

	REQUIRE(MPSCQueueTryPush(mpsc, as_ptr(0x06)) == true);

	elem = NULL;
	REQUIRE(MPSCQueueTryPop(mpsc, &elem) == true);
	REQUIRE(elem == as_ptr(0x02));

	elem = NULL;
	REQUIRE(MPSCQueueTryPop(mpsc, &elem) == true);
	REQUIRE(elem == as_ptr(0x04));

	elem = NULL;
	REQUIRE(MPSCQueueTryPop(mpsc, &elem) == true);
	REQUIRE(elem == as_ptr(0x06));
}
