#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include "mpsc_shmem_queue.hpp"

#define as_ptr(x) (reinterpret_cast<void *>(x))
#define as_int(x) (reinterpret_cast<uintptr_t>(x))

static std::mutex m;

static void
mpsc_consume_worker(mpsc_shemem_queue_t &mpsc, std::vector<uintptr_t> &out, int total_items)
{
	int num_times_waited = 0;
	int num_consumed     = 0;

	using namespace std::literals::chrono_literals;

	auto pred = [&num_consumed, total_items, &mpsc]() {
		if (num_consumed >= total_items)
			return true;

		return !mpsc.is_empty();
	};

	while (num_consumed < total_items)
	{
		void *elem;

		if (mpsc.try_pop(elem))
		{
			out.emplace_back(as_int(elem));
			mpsc.wakeup_one_producer();
			num_consumed++;
		}
		else
		{
			mpsc.consumer_wait(pred);
			num_times_waited++;
		}
	}

	mpsc.wakeup_all_producers();
	mpsc.wakeup_all_consumers();

	m.lock();
	std::cout << "Consumer Num times waited: " << num_times_waited << std::endl;
	m.unlock();
}

static void
mpsc_produce_worker(mpsc_shemem_queue_t &mpsc, std::vector<uintptr_t> &in)
{
	int num_times_waited = 0;

	auto pred = [&mpsc]() { return !mpsc.is_full(); };

	for (auto &elem : in)
	{
		while (!mpsc.try_push(as_ptr(elem)))
		{
			mpsc.producer_wait(pred);
			num_times_waited++;
		}

		mpsc.wakeup_one_consumer();
	}

	mpsc.wakeup_all_consumers();

	m.lock();
	std::cout << "Produer Num times waited: " << num_times_waited << std::endl;
	m.unlock();
}

static void
start_producers(mpsc_shemem_queue_t &mpsc,
                std::vector<uintptr_t> *ins,
                int num_producers,
                std::vector<std::thread> &producers)
{
	for (int i = 0; i < num_producers; i++)
	{
		producers.emplace_back(
		    std::thread{ mpsc_produce_worker, std::ref(mpsc), std::ref(ins[i]) });
	}
}

static void
fill_vec(std::vector<uintptr_t> &vec, int num_times)
{
	vec.reserve(num_times);

	for (int i = 0; i < num_times; i++)
		vec.emplace_back(i * 2);
}

int
mpsc_bench_main(int argc, char *argv[])
{
	if (argc != 5 && argc != 6)
	{
		std::cerr << "Usage: " << argv[0] << " queue_size num_items num_producers [verify]\n";
		return -1;
	}

	int queue_size, num_times, num_producers, verify = 0;

	std::sscanf(argv[2], "%d", &queue_size);
	std::sscanf(argv[3], "%d", &num_times);
	std::sscanf(argv[4], "%d", &num_producers);

	if (argc == 6)
		std::sscanf(argv[5], "%d", &verify);

	auto ins = std::make_unique<std::vector<uintptr_t>[]>(num_producers);
	std::vector<uintptr_t> out;
	std::vector<std::thread> producers;
	mpsc_shemem_queue_t mpsc{ queue_size, num_producers };

	for (int i = 0; i < num_producers; i++)
		fill_vec(ins[i], num_times);

	auto start = std::chrono::system_clock::now();

	start_producers(mpsc, ins.get(), num_producers, producers);
	mpsc_consume_worker(mpsc, out, num_times * num_producers);

	for (auto &worker : producers)
		worker.join();

	auto end = std::chrono::system_clock::now();

	if (verify)
	{
		std::vector<uintptr_t> sort_in;

		sort_in.reserve(num_times * num_producers);

		for (int i = 0; i < num_producers; i++)
			sort_in.insert(sort_in.end(), ins[i].begin(), ins[i].end());

		std::sort(sort_in.begin(), sort_in.end());
		std::sort(out.begin(), out.end());

		if (sort_in != out)
		{
			std::cerr << "Queue has problem\n";
			return 1;
		}
	}

	std::chrono::duration<double> elapsed_seconds = end - start;

	std::cout << "elapsed time: " << elapsed_seconds.count() << "s\n";

	return 0;
}