#include "catch.hpp"

#include "cqueue.hpp"

#define as_ptr(x) (reinterpret_cast<void *>(x))

TEST_CASE("Basic Test", "[cqueue]")
{
	cqueue_t mpsc{ 3, 1 };
	std::vector<void *> pointers = { as_ptr(0x00), as_ptr(0x02), as_ptr(0x04) };
	void *elem;

	REQUIRE(mpsc.try_pop(elem) == false);

	REQUIRE(mpsc.try_push(as_ptr(0x00)) == true);
	REQUIRE(mpsc.try_push(as_ptr(0x02)) == true);
	REQUIRE(mpsc.try_push(as_ptr(0x04)) == true);

	REQUIRE(mpsc.try_push(as_ptr(0x06)) == false);

	elem = NULL;
	REQUIRE(mpsc.try_pop(elem) == true);
	REQUIRE(elem == as_ptr(0x00));

	REQUIRE(mpsc.try_push(as_ptr(0x06)) == true);

	elem = NULL;
	REQUIRE(mpsc.try_pop(elem) == true);
	REQUIRE(elem == as_ptr(0x02));

	elem = NULL;
	REQUIRE(mpsc.try_pop(elem) == true);
	REQUIRE(elem == as_ptr(0x04));

	elem = NULL;
	REQUIRE(mpsc.try_pop(elem) == true);
	REQUIRE(elem == as_ptr(0x06));
}
