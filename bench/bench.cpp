#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>
#include <vector>

#include <lockfree-queue/mpmc.h>
#include <lockfree-queue/mpsc.h>
#include <lockfree-queue/mpsc_pc.h>

#include "Barrier.h"
#include "waitevent.h"


#ifndef _MSC_VER
#define sscanf_s(...) std::sscanf(__VA_ARGS__)
#endif

using namespace lockfree::thread;

template <typename T> struct CQueueBase
{
	virtual ~CQueueBase() = default;

	virtual auto TryPush(int pid, const T& val) noexcept -> std::optional<T> = 0;
	virtual auto TryPop(int pid) noexcept -> std::optional<T> = 0;

	virtual auto IsFull() noexcept -> bool = 0;
	virtual auto IsEmpty() noexcept -> bool = 0;
};

template <typename T> class CQueue
{
public:
	using value_type = T;

	explicit CQueue(std::shared_ptr<CQueueBase<T>> queue) : m_queue(std::move(queue)) {}

	auto TryPush(int pid, const T& val) noexcept -> std::optional<T>
	{
		return m_queue->TryPush(pid, val);
	}
	auto TryPop(int pid) noexcept -> std::optional<T> { return m_queue->TryPop(pid); }

	auto IsFull() noexcept -> bool { return m_queue->IsFull(); }
	auto IsEmpty() noexcept -> bool { return m_queue->IsEmpty(); }

private:
	std::shared_ptr<CQueueBase<T>> m_queue;
};

template <typename T> class Bench
{
public:
	explicit Bench(CQueue<T> queue) : m_queue(std::move(queue)) {}

	auto start(std::size_t num_times, int num_producers, int num_consumers, bool verify) -> int
	{
		Barrier start_bench_barrier;
		auto& queue = m_queue;
		auto cout_sync_m = std::make_shared<std::mutex>();
		auto producers_we = std::make_shared<WaitEvent>();
		auto consumers_we = std::make_shared<WaitEvent>();

		auto [producers, input] = start_producers(queue, num_producers, 0, num_times, producers_we,
			consumers_we, cout_sync_m, start_bench_barrier);
		auto [consumers, output] = start_consumers(std::move(queue), num_consumers, num_producers,
			num_producers * num_times, producers_we, consumers_we, cout_sync_m,
			start_bench_barrier);

		auto start = std::chrono::system_clock::now();

		std::move(start_bench_barrier).Notify();

		for (auto& worker : producers)
			worker.join();

		for (auto& worker : consumers)
			worker.join();

		auto end = std::chrono::system_clock::now();

		if (verify)
		{
			std::vector<T> sort_in;
			std::vector<T> sort_out;

			sort_in.reserve(num_times * num_producers);
			sort_out.reserve(num_times * num_producers);

			for (int i = 0; i < num_producers; i++)
			{
				sort_in.insert(sort_in.end(), input[i]->begin(), input[i]->end());
			}

			for (int i = 0; i < num_consumers; i++)
			{
				sort_out.insert(sort_out.end(), output[i]->begin(), output[i]->end());
			}

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
		return 0;
	}

private:
	struct ConsumerData
	{
		CQueue<T> queue;
		int pid;
		std::shared_ptr<std::vector<T>> out;
		std::shared_ptr<std::atomic<std::size_t>> num_consumed;
		std::size_t total_items;
		std::shared_ptr<WaitEvent> producers;
		std::shared_ptr<WaitEvent> consumers;

		std::shared_ptr<std::mutex> cout_sync_m;
		Barrier start_bench_barrier;
	};

	struct ProducerData
	{
		CQueue<T> queue;
		int pid;
		std::shared_ptr<std::vector<T>> in;
		std::shared_ptr<WaitEvent> producers;
		std::shared_ptr<WaitEvent> consumers;

		std::shared_ptr<std::mutex> cout_sync_m;
		Barrier start_bench_barrier;
	};

	static void consumer(ConsumerData cdata)
	{
		std::size_t num_times_waited = 0;
		std::size_t local_consumed = 0;

		std::move(cdata.start_bench_barrier).Wait();

		auto pred = [&cdata] {
			if (cdata.num_consumed->load() >= cdata.total_items)
				return true;

			return !cdata.queue.IsEmpty();
		};

		while (cdata.num_consumed->load() < cdata.total_items)
		{
			if (auto val = cdata.queue.TryPop(cdata.pid))
			{
				cdata.out->emplace_back(*val);
				cdata.producers->WakeupOneWaiter();
				local_consumed++;
			}
			else
			{
				*cdata.num_consumed += std::exchange(local_consumed, 0);
				adaptive_wait(pred, *cdata.consumers);
				num_times_waited++;
			}
		}

		*cdata.num_consumed += std::exchange(local_consumed, 0);

		cdata.producers->WakeupAllWaiters();
		cdata.consumers->WakeupAllWaiters();

		{
			std::lock_guard l(*cdata.cout_sync_m);
			std::cout << "Consumer Num times waited: " << num_times_waited << std::endl;
		}
	}

	static void producer(ProducerData pdata)
	{
		std::size_t num_times_waited = 0;

		std::move(pdata.start_bench_barrier).Wait();

		auto pred = [&pdata] { return !pdata.queue.IsFull(); };

		for (auto elem : *pdata.in)
		{
			while (!pdata.queue.TryPush(pdata.pid, elem))
			{
				adaptive_wait(pred, *pdata.producers);
				num_times_waited++;
			}

			pdata.consumers->WakeupOneWaiter();
		}

		pdata.consumers->WakeupAllWaiters();

		{
			std::lock_guard l(*pdata.cout_sync_m);
			std::cout << "Producer Num times waited: " << num_times_waited << std::endl;
		}
	}

	static auto start_consumers(CQueue<T> queue, int num_consumers, int pid_start,
		std::size_t total_items, const std::shared_ptr<WaitEvent>& producers_we,
		const std::shared_ptr<WaitEvent>& consumers_we,
		const std::shared_ptr<std::mutex>& cout_sync_m, const Barrier& start_bench_barrier)
		-> std::pair<std::vector<std::thread>, std::vector<std::shared_ptr<std::vector<T>>>>
	{
		std::vector<std::thread> threads;
		std::vector<std::shared_ptr<std::vector<T>>> output;

		auto num_consumed = std::make_shared<std::atomic<std::size_t>>(0);
		ConsumerData cdata_base = { std::move(queue), 0, {}, num_consumed, total_items,
			producers_we, consumers_we, cout_sync_m, start_bench_barrier };

		for (int i = 0; i < num_consumers; i++)
		{
			auto cdata = cdata_base;
			auto out = std::make_shared<std::vector<T>>();

			cdata.out = out;
			cdata.pid = i + pid_start;

			output.push_back(std::move(out));
			threads.emplace_back(consumer, std::move(cdata));
		}

		return { std::move(threads), std::move(output) };
	}

	static auto gen_input(std::size_t num_times) -> std::vector<T>
	{
		std::vector<T> vec;
		std::mt19937_64 gen(std::random_device{}());
		std::uniform_int_distribution<T> dist;

		vec.reserve(num_times);

		for (std::size_t i = 0; i < num_times; i++)
		{
			vec.emplace_back(dist(gen));
		}

		return vec;
	}

	static auto start_producers(CQueue<T> queue, int num_producers, int pid_start,
		std::size_t num_items, const std::shared_ptr<WaitEvent>& producers_we,
		const std::shared_ptr<WaitEvent>& consumers_we,
		const std::shared_ptr<std::mutex>& cout_sync_m, const Barrier& start_bench_barrier)
		-> std::pair<std::vector<std::thread>, std::vector<std::shared_ptr<std::vector<T>>>>
	{
		std::vector<std::thread> threads;
		std::vector<std::shared_ptr<std::vector<T>>> input;
		ProducerData pdata_base = { std::move(queue), 0, {}, producers_we, consumers_we,
			cout_sync_m, start_bench_barrier };

		for (int i = 0; i < num_producers; i++)
		{
			auto pdata = pdata_base;
			auto in = std::make_shared<std::vector<T>>(gen_input(num_items));

			pdata.pid = i + pid_start;
			pdata.in = in;

			input.push_back(std::move(in));
			threads.emplace_back(producer, std::move(pdata));
		}

		return { std::move(threads), std::move(input) };
	}

	template <typename Predicate> static void adaptive_wait(Predicate&& predicate, WaitEvent& we)
	{
		auto available = [&] {
			constexpr auto MAX_SPIN = 1000;
			lockfree::ExponentialBackoff backoff(1, MAX_SPIN);

			for (int i = 0; i < MAX_SPIN; i++)
			{
				backoff();
				if (std::invoke(predicate))
					return true;
			}

			return false;
		}();

		if (!available)
			we.Wait(std::forward<Predicate>(predicate));
	}

	CQueue<T> m_queue;
};


template <typename T> struct MPMCQueueWrapper : public CQueueBase<T>
{
	MPMCQueueWrapper(int max_processes, std::size_t queue_size) : queue(max_processes, queue_size)
	{
	}

	auto TryPush(int pid, const T& val) noexcept -> std::optional<T> override
	{
		return queue.TryPush(pid, val);
	}
	auto TryPop(int pid) noexcept -> std::optional<T> override { return queue.TryPop(pid); }

	auto IsFull() noexcept -> bool override { return queue.IsFull(); }
	auto IsEmpty() noexcept -> bool override { return queue.IsEmpty(); }

private:
	MPMCQueue<T> queue;
};

template <typename T> struct MPSCQueueWrapper : public CQueueBase<T>
{
	MPSCQueueWrapper(int max_processes, std::size_t queue_size) : queue(max_processes, queue_size)
	{
	}

	auto TryPush(int pid, const T& val) noexcept -> std::optional<T> override
	{
		return queue.TryPush(pid, val);
	}
	auto TryPop(int /*pid*/) noexcept -> std::optional<T> override { return queue.TryPop(); }

	auto IsFull() noexcept -> bool override { return queue.IsFull(); }
	auto IsEmpty() noexcept -> bool override { return queue.IsEmpty(); }

private:
	MPSCQueue<T> queue;
};

template <typename T> struct MPSCPCQueueWrapper : public CQueueBase<T>
{
	MPSCPCQueueWrapper(int /*max_processes*/, std::size_t queue_size)
		: queue((sizeof(MPSCPCQueueAny::size_type) + sizeof(T)) * queue_size)
	{
	}

	auto TryPush(int /*pid*/, const T& val) noexcept -> std::optional<T> override
	{
		return queue.TryPush(reinterpret_cast<const char*>(&val), sizeof(T));
	}
	auto TryPop(int /*pid*/) noexcept -> std::optional<T> override
	{
		T val;
		if (queue.TryPop(reinterpret_cast<void*>(&val)))
			return val;
		return {};
	}

	auto IsFull() noexcept -> bool override { return queue.IsFull(); }
	auto IsEmpty() noexcept -> bool override { return queue.IsEmpty(); }

private:
	MPSCPCQueueAny queue;
};

auto main(int argc, char** argv) -> int
{
	auto print_help = [&] {
		std::cerr
			<< "Usage: " << argv[0]
			<< " queue_type[= mpmc/mpsc/mpsc-pc] num_items num_producers num_consumers [verify]\n";
	};
	if (argc != 5 && argc != 6)
	{
		print_help();
		return -1;
	}

	constexpr std::string_view MPMC = "mpmc";
	constexpr std::string_view MPSC = "mpsc";
	constexpr std::string_view MPSC_PC = "mpsc-pc";

	std::string queue_type;
	std::size_t num_times;
	int num_producers;
	int num_consumers;
	bool verify = false;

	std::istringstream(argv[1]) >> queue_type;
	std::istringstream(argv[2]) >> num_times;
	std::istringstream(argv[3]) >> num_producers;
	std::istringstream(argv[4]) >> num_consumers;

	if (argc == 6)
		std::istringstream(argv[5]) >> std::boolalpha >> verify;

	using T = std::uint64_t;
	std::optional<CQueue<T>> queue;

	if (queue_type == MPMC)
	{
		queue.emplace(std::make_shared<MPMCQueueWrapper<T>>(
			num_producers + num_consumers, num_producers * num_times));
	}
	else if (queue_type == MPSC)
	{
		if (num_consumers != 1)
		{
			std::cerr << "WARNING: MPSC queue will have only one consumer. Running MPSC bench with "
						 "one consumer.\n";
		}
		num_consumers = 1;
		queue.emplace(std::make_shared<MPSCQueueWrapper<T>>(
			num_producers + num_consumers, num_producers * num_times));
	}
	else if (queue_type == MPSC_PC)
	{
		if (num_consumers != 1)
		{
			std::cerr
				<< "WARNING: MPSC-PC queue will have only one consumer. Running MPSC bench with "
				   "one consumer.\n";
		}
		num_consumers = 1;
		queue.emplace(std::make_shared<MPSCPCQueueWrapper<T>>(
			num_producers + num_consumers, num_producers * num_times));
	}
	else
	{
		print_help();
		return 1;
	}

	return Bench(*std::move(queue)).start(num_times, num_producers, num_consumers, verify);
}