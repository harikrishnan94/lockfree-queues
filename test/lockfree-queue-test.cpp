#include <array>
#include <doctest/doctest.h>
#include <string_view>

#include <lockfree-queue/mpmc.h>
#include <lockfree-queue/mpsc.h>
#include <lockfree-queue/mpsc_pc.h>
#include <lockfree-queue/spsc.h>

#include "string-gen.h"

using namespace lockfree::thread;

TEST_SUITE("MPMC") // NOLINT
{
	TEST_CASE("Basic")
	{
		MPMCQueue<int> queue(1, 3);

		REQUIRE(queue.TryPush(0, 1) == true);
		REQUIRE(queue.TryPush(0, 2) == true);
		REQUIRE(queue.TryPush(0, 3) == true);

		REQUIRE(queue.TryPush(0, 4) == false);

		int val;
		REQUIRE(queue.TryPop(0, val) == true);
		REQUIRE(val == 1);
		REQUIRE(queue.TryPop(0, val) == true);
		REQUIRE(val == 2);
		REQUIRE(queue.TryPop(0, val) == true);
		REQUIRE(val == 3);

		REQUIRE(queue.TryPop(0, val) == false);
	}
}

TEST_SUITE("MPSC") // NOLINT
{
	TEST_CASE("Basic")
	{
		MPSCQueue<int> queue(1, 3);

		REQUIRE(queue.TryPush(0, 1) == true);
		REQUIRE(queue.TryPush(0, 2) == true);
		REQUIRE(queue.TryPush(0, 3) == true);

		REQUIRE(queue.TryPush(0, 4) == false);

		int val;
		REQUIRE(queue.TryPop(val) == true);
		REQUIRE(val == 1);
		REQUIRE(queue.TryPop(val) == true);
		REQUIRE(val == 2);
		REQUIRE(queue.TryPeek(val) == true);
		REQUIRE(val == 3);
		REQUIRE(queue.TryPop(val) == true);
		REQUIRE(val == 3);

		REQUIRE(queue.TryPeek(val) == false);
		REQUIRE(queue.TryPop(val) == false);
	}

	void push(MPSCQueueAny queue, size_t count, int pid)
	{
		StringGen str;

		for (size_t i = 0; i < count; i++)
		{
			while (!queue.TryPush(pid, str()))
				;
		}
	}

	TEST_CASE("WrapAround")
	{
		static constexpr auto QSIZE = StringGen::AVGLEN * 100;
		constexpr auto TEST_ITER = 5000;

		MPSCQueueAny queue(1, QSIZE);
		std::vector<std::thread> producers;
		std::thread consumer{ pop<MPSCQueueAny>, queue, TEST_ITER };
		std::thread producer{ push, queue, TEST_ITER, 0 };

		consumer.join();
		producer.join();
	}
}

TEST_SUITE("SPSC") // NOLINT
{
	TEST_CASE("Basic")
	{
		SPSCQueue<int> queue(3);

		REQUIRE(queue.TryPush(1) == true);
		REQUIRE(queue.TryPush(2) == true);
		REQUIRE(queue.TryPush(3) == true);

		REQUIRE(queue.TryPush(4) == false);

		int val;
		REQUIRE(queue.TryPop(val) == true);
		REQUIRE(val == 1);
		REQUIRE(queue.TryPop(val) == true);
		REQUIRE(val == 2);
		REQUIRE(queue.TryPeek(val) == true);
		REQUIRE(val == 3);
		REQUIRE(queue.TryPop(val) == true);
		REQUIRE(val == 3);

		REQUIRE(queue.TryPeek(val) == false);
		REQUIRE(queue.TryPop(val) == false);
	}

	void push(SPSCQueueAny queue, size_t count)
	{
		StringGen str;

		for (size_t i = 0; i < count; i++)
		{
			while (!queue.TryPush(str()))
				;
		}
	}

	TEST_CASE("WrapAround")
	{
		static constexpr auto QSIZE = StringGen::AVGLEN * 100;
		constexpr auto TEST_ITER = 5000;

		SPSCQueueAny queue(QSIZE);
		std::vector<std::thread> producers;
		std::thread consumer{ pop<SPSCQueueAny>, queue, TEST_ITER };
		std::thread producer{ push, queue, TEST_ITER };

		consumer.join();
		producer.join();
	}
}

TEST_SUITE("MPSC-PC") // NOLINT
{
	TEST_CASE("Basic")
	{
		constexpr auto QLEN = 3 * sizeof(MPSCPCQueueAny::size_type) + 6;

		constexpr std::string_view DATA1 = { "a" };
		constexpr std::string_view DATA2 = { "ab" };
		constexpr std::string_view DATA3 = { "abc" };

		REQUIRE(lockfree::MPSCPCQueueAny::Available());

		MPSCPCQueueAny queue(QLEN);

		REQUIRE(queue.TryPush(DATA1) == true);
		REQUIRE(queue.TryPush(DATA2) == true);
		REQUIRE(queue.TryPush(DATA3) == true);

		REQUIRE(queue.TryPush(DATA1) == false);

		std::array<char, QLEN> outdata;
		auto try_pop = [&] {
			outdata = {};
			return queue.TryPop(outdata.data());
		};

		REQUIRE(try_pop() == true);
		REQUIRE(outdata.data() == DATA1);
		REQUIRE(try_pop() == true);
		REQUIRE(outdata.data() == DATA2);
		REQUIRE(try_pop() == true);
		REQUIRE(outdata.data() == DATA3);

		REQUIRE(try_pop() == false);

		// Test for wrap around
		REQUIRE(queue.TryPush(DATA2) == true);
		REQUIRE(queue.TryPush(DATA3) == true);
		REQUIRE(try_pop() == true);
		REQUIRE(outdata.data() == DATA2);
		REQUIRE(queue.TryPush(DATA3) == true);
		REQUIRE(try_pop() == true);
		REQUIRE(outdata.data() == DATA3);
	}

	void push(MPSCPCQueueAny queue, size_t count)
	{
		StringGen str;

		for (size_t i = 0; i < count; i++)
		{
			while (!queue.TryPush(str()))
				;
		}
	}

	[[nodiscard]] auto set_cpu(std::thread & t, int cpu)
	{
		cpu_set_t cpuset;
		CPU_ZERO(&cpuset);
		CPU_SET(cpu, &cpuset);
		return pthread_setaffinity_np(t.native_handle(), sizeof(cpuset), &cpuset);
	}

	static constexpr auto QSIZE = StringGen::AVGLEN * 100;

	TEST_CASE("ThreadMigration")
	{
		constexpr auto TEST_ITER = 5000;

		MPSCPCQueueAny queue(QSIZE);
		std::vector<std::thread> producers;
		std::thread consumer{ pop<MPSCPCQueueAny>, queue, TEST_ITER };
		std::thread producer{ push, queue, TEST_ITER };
		std::atomic<bool> exit = false;
		std::thread migrator{ [&] {
			const auto num_cores = int(std::thread::hardware_concurrency());
			int cpu = 0;

			while (true)
			{
				if (exit.load() || set_cpu(producer, cpu) != 0)
				{
					producer.join();
					break;
				}

				if (++cpu == num_cores)
					cpu = 0;
			}
		} };

		consumer.join();
		exit = true;
		migrator.join();
	}

	TEST_CASE("Concurrency")
	{
		constexpr auto TEST_ITER = 25000;
		MPSCPCQueueAny queue(QSIZE);
		std::thread consumer{ pop<MPSCPCQueueAny>, queue, TEST_ITER * 2 };
		std::thread producer1{ push, queue, TEST_ITER };
		std::thread producer2{ push, queue, TEST_ITER };
		std::atomic<bool> exit = false;
		std::thread migrator{ [&] {
			int cpu1 = 0;
			int cpu2 = 1;

			while (true)
			{
				if (exit.load() || set_cpu(producer1, cpu1) != 0 || set_cpu(producer2, cpu2) != 0)
				{
					producer1.join();
					producer2.join();
					break;
				}

				std::swap(cpu1, cpu2);
			}
		} };

		consumer.join();
		exit = true;
		migrator.join();
	}

	TEST_CASE("Stress")
	{
		constexpr auto TEST_ITER = 250000;
		constexpr auto STRESS_FACTOR = 5;

		const auto num_threads = STRESS_FACTOR * int(std::thread::hardware_concurrency());
		MPSCPCQueueAny queue(QSIZE);
		std::vector<std::thread> producers;
		std::thread consumer{ pop<MPSCPCQueueAny>, queue, TEST_ITER * num_threads };

		producers.reserve(num_threads);
		for (int i = 0; i < num_threads; i++)
		{
			producers.emplace_back(push, queue, TEST_ITER);
		}

		for (auto& p : producers)
		{
			p.join();
		}
		consumer.join();
	}
}
