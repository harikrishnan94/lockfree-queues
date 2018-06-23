#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include "cqueue.hpp"

#define as_ptr(x) (reinterpret_cast<void *>(x))
#define as_int(x) (reinterpret_cast<uintptr_t>(x))

std::mutex m;

static void
cqueue_consume_worker(cqueue_t &mpsc,
                      std::vector<uintptr_t> &out,
                      std::atomic_int &num_consumed,
                      int total_items)
{
	using namespace std::chrono_literals;
	int num_times_waited = 0;

	while (num_consumed < total_items)
	{
		void *elem;

		if (mpsc.try_pop(elem))
		{
			out.emplace_back(as_int(elem));
			num_consumed++;
		}
		else
		{
			num_times_waited++;
			std::this_thread::sleep_for(10us);
		}
	}

	m.lock();
	std::cout << "Consumer Num times waited: " << num_times_waited << std::endl;
	m.unlock();
}

static void
cqueue_produce_worker(cqueue_t &mpsc, std::vector<uintptr_t> &in)
{
	using namespace std::chrono_literals;
	int num_times_waited = 0;

	for (auto &elem : in)
	{
		while (!mpsc.try_push(as_ptr(elem)))
		{
			num_times_waited++;
			std::this_thread::sleep_for(10us);
		}
	}

	m.lock();
	std::cout << "Produer Num times waited: " << num_times_waited << std::endl;
	m.unlock();
}

static void
start_consumers(cqueue_t &mpsc,
                int total_items,
                std::vector<uintptr_t> *outs,
                int num_consumers,
                std::atomic_int &num_consumed,
                std::vector<std::thread> &consumers)
{
	for (int i = 0; i < num_consumers; i++)
	{
		consumers.emplace_back(std::thread{ cqueue_consume_worker,
		                                    std::ref(mpsc),
		                                    std::ref(outs[i]),
		                                    std::ref(num_consumed),
		                                    total_items });
	}
}

static void
start_producers(cqueue_t &mpsc,
                std::vector<uintptr_t> *ins,
                int num_producers,
                std::vector<std::thread> &producers)
{
	for (int i = 0; i < num_producers; i++)
	{
		producers.emplace_back(
		    std::thread{ cqueue_produce_worker, std::ref(mpsc), std::ref(ins[i]) });
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
main(int argc, char *argv[])
{
	if (argc != 5 && argc != 6)
	{
		std::cerr << "Usage: " << argv[0]
		          << " queue_size num_items num_producers num_consumers [verify]\n";
		return -1;
	}

	int queue_size, num_times, num_producers, num_consumers, verify = 0;

	std::sscanf(argv[1], "%d", &queue_size);
	std::sscanf(argv[2], "%d", &num_times);
	std::sscanf(argv[3], "%d", &num_producers);
	std::sscanf(argv[4], "%d", &num_consumers);

	if (argc == 6)
		std::sscanf(argv[5], "%d", &verify);

	auto ins  = std::make_unique<std::vector<uintptr_t>[]>(num_producers);
	auto outs = std::make_unique<std::vector<uintptr_t>[]>(num_consumers);
	std::vector<std::thread> producers, consumers;
	cqueue_t mpsc{ queue_size, num_producers + num_consumers };
	std::atomic_int num_consumed{ 0 };

	for (int i = 0; i < num_producers; i++)
		fill_vec(ins[i], num_times);

	auto start = std::chrono::system_clock::now();

	start_producers(mpsc, ins.get(), num_producers, producers);
	start_consumers(mpsc,
	                num_times * num_producers,
	                outs.get(),
	                num_consumers,
	                num_consumed,
	                consumers);

	for (auto &worker : producers)
		worker.join();

	for (auto &worker : consumers)
		worker.join();

	auto end = std::chrono::system_clock::now();

	if (verify)
	{

		std::vector<uintptr_t> sort_in, sort_out;

		sort_in.reserve(num_times * num_producers);
		sort_out.reserve(num_times * num_consumers);

		for (int i = 0; i < num_producers; i++)
			sort_in.insert(sort_in.end(), ins[i].begin(), ins[i].end());

		for (int i = 0; i < num_consumers; i++)
			sort_out.insert(sort_out.end(), outs[i].begin(), outs[i].end());

		std::sort(sort_in.begin(), sort_in.end());
		std::sort(sort_out.begin(), sort_out.end());

		if (sort_in != sort_out)
		{
			std::cerr << "Queue has problem\n";
			return 1;
		}
	}

	std::chrono::duration<double> elapsed_seconds = end - start;

	std::cout << "elapsed time: " << elapsed_seconds.count() << "s\n";
}
