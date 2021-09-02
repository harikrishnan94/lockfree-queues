#pragma once

#include <boost/align/align_up.hpp>
#include <boost/align/aligned_alloc.hpp>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>

#include "lockfree-queue/backoff.h"
#include "lockfree-queue/detail/defs.h"
#include "lockfree-queue/detail/scopeexit.h"


namespace lockfree
{
	template <typename T> class alignas(std::max(detail::CACHELINESIZE, alignof(T))) MPMCQueue
	{
		static_assert(std::is_trivial_v<T>, "Type must be trivial to be store inside queue");

	public:
		using size_type = std::size_t;
		using value_type = T;

		static auto CalculateSize(int max_processes, size_type queue_size) noexcept -> size_type
		{
			auto size = sizeof(MPMCQueue);

			static_assert(std::is_trivially_copyable_v<MPMCQueue>);

			size = boost::alignment::align_up(size, alignof(ThreadPos));
			size += sizeof(ThreadPos) * max_processes;

			return boost::alignment::align_up(size, alignof(MPMCQueue)) +
				   queue_size * sizeof(value_type);
		}

		static auto Initialize(void* queue_ptr, int max_processes, size_type queue_size) noexcept
			-> MPMCQueue*
		{
			// NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
			return new (static_cast<MPMCQueue*>(queue_ptr)) MPMCQueue(max_processes, queue_size);
		}

		auto TryPush(int pid, const value_type& val) noexcept -> bool
		{
			SCOPE_EXIT([&] { detail::store_release(get_tpos_data()[pid].head, INVALID_Q_POS); });

			if (auto head = reserve_head_to_produce(pid))
			{
				get_queue_data()[*head % m_queue_size] = val;
				return true;
			}

			return false;
		}

		auto TryPop(int pid) noexcept -> std::optional<value_type>
		{
			SCOPE_EXIT([&] { detail::store_release(get_tpos_data()[pid].tail, INVALID_Q_POS); });

			if (auto tail = reserve_tail_to_consume(pid))
				return get_queue_data()[*tail % m_queue_size];

			return {};
		}

		// Use this variant to avoid need to double copy.
		auto TryPop(int pid, value_type& outval) noexcept -> bool
		{
			SCOPE_EXIT([&] { detail::store_release(get_tpos_data()[pid].tail, INVALID_Q_POS); });

			if (auto tail = reserve_tail_to_consume(pid))
			{
				outval = get_queue_data()[*tail % m_queue_size];
				return true;
			}
			return false;
		}

		auto IsEmpty() noexcept -> bool
		{
			size_type last_head;
			auto is_empty = [&] {
				return this->is_empty(
					(last_head = detail::load_acquire(m_last_head)), detail::load_acquire(m_tail));
			};
			if (is_empty())
			{
				update_last_head(last_head);
				return is_empty();
			}

			return false;
		}

		auto IsFull() noexcept -> bool
		{
			size_type last_tail;
			auto is_full = [&] {
				return this->is_full(
					detail::load_acquire(m_head), (last_tail = detail::load_acquire(m_last_tail)));
			};
			if (is_full())
			{
				update_last_tail(last_tail);
				return is_full();
			}

			return false;
		}

	private:
		static constexpr auto INVALID_Q_POS = std::numeric_limits<size_type>::max();

		struct alignas(detail::CACHELINESIZE) ThreadPos
		{
			std::atomic<size_type> head = INVALID_Q_POS;
			std::atomic<size_type> tail = INVALID_Q_POS;
		};

		MPMCQueue(int max_processes, size_type queue_size) noexcept
			: m_max_processes(max_processes), m_queue_size(queue_size)
		{
			auto* tpos = get_tpos_data();
			for (int i = 0; i < max_processes; i++)
				new (&tpos[i]) ThreadPos{};
		}


		void update_last_tail(size_type old_last_tail) noexcept
		{
			auto last_tail = detail::load_acquire(m_tail);
			const auto* tpos = get_tpos_data();

			for (int i = 0; i < m_max_processes; i++)
			{
				auto tail = detail::load_acquire(tpos[i].tail);
				last_tail = std::min(tail, last_tail);
			}

			if (last_tail > old_last_tail &&
				!m_last_tail.compare_exchange_strong(old_last_tail, last_tail) &&
				old_last_tail < last_tail)
			{
				update_last_tail(old_last_tail);
			}
		}

		void update_last_head(size_type old_last_head) noexcept
		{
			auto last_head = detail::load_acquire(m_head);
			const auto* tpos = get_tpos_data();

			for (int i = 0; i < m_max_processes; i++)
			{
				auto head = detail::load_acquire(tpos[i].head);
				last_head = std::min(last_head, head);
			}

			if (last_head > old_last_head &&
				!m_last_head.compare_exchange_strong(old_last_head, last_head) &&
				old_last_head < last_head)
			{
				update_last_head(old_last_head);
			}
		}


		template <bool TryAgain = true>
		auto reserve_head_to_produce(int pid) noexcept -> std::optional<size_type>
		{
			auto head = detail::load_acquire(m_head);
			auto last_tail = detail::load_acquire(m_last_tail);
			auto* tpos = get_tpos_data();
			ExponentialBackoff backoff;

			while (!is_full(head, last_tail))
			{
				detail::store_release(tpos[pid].head, head);

				if (m_head.compare_exchange_strong(head, head + 1))
					return head;

				backoff();

				head = detail::load_acquire(m_head);
				last_tail = detail::load_acquire(m_last_tail);
			}

			if constexpr (TryAgain)
			{
				update_last_tail(last_tail);
				return reserve_head_to_produce<false>(pid);
			}

			return {};
		}

		template <bool TryAgain = true>
		auto reserve_tail_to_consume(int pid) -> std::optional<size_type>
		{
			auto last_head = detail::load_acquire(m_last_head);
			auto tail = detail::load_acquire(m_tail);
			auto* tpos = get_tpos_data();
			ExponentialBackoff backoff;

			while (!is_empty(last_head, tail))
			{
				detail::store_release(tpos[pid].tail, tail);

				if (m_tail.compare_exchange_strong(tail, tail + 1))
					return tail;

				backoff();

				last_head = detail::load_acquire(m_last_head);
				tail = detail::load_acquire(m_tail);
			}

			if constexpr (TryAgain)
			{
				update_last_head(last_head);
				return reserve_tail_to_consume<false>(pid);
			}

			return {};
		}


		[[nodiscard]] auto is_full(size_type head, size_type tail) const noexcept -> bool
		{
			return head >= tail + m_queue_size;
		}
		static auto is_empty(size_type head, size_type tail) noexcept -> bool
		{
			return tail >= head;
		}


		auto get_tpos_data() noexcept -> ThreadPos*
		{
			auto* p = reinterpret_cast<char*>(this);
			return static_cast<ThreadPos*>(
				boost::alignment::align_up(p + sizeof(MPMCQueue), alignof(ThreadPos)));
		}
		[[nodiscard]] auto get_tpos_data() const noexcept -> const ThreadPos*
		{
			// NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
			return const_cast<MPMCQueue*>(this)->get_tpos_data();
		}

		auto get_queue_data() noexcept -> T*
		{
			auto* p = reinterpret_cast<char*>(get_tpos_data());
			return static_cast<T*>(boost::alignment::align_up(
				p + sizeof(ThreadPos) * m_max_processes, detail::CACHELINESIZE));
		}
		[[nodiscard]] auto get_queue_data() const noexcept -> const T*
		{
			// NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
			return const_cast<MPMCQueue*>(this)->get_queue_data();
		}


		const int m_max_processes;
		const size_type m_queue_size;

		alignas(detail::CACHELINESIZE) std::atomic<size_type> m_head = 0;
		alignas(detail::CACHELINESIZE) std::atomic<size_type> m_tail = 0;
		alignas(detail::CACHELINESIZE) std::atomic<size_type> m_last_head = 0;
		alignas(detail::CACHELINESIZE) std::atomic<size_type> m_last_tail = 0;
	};

	namespace thread
	{
		template <typename T> class MPMCQueue
		{
		public:
			using size_type = std::size_t;
			using value_type = T;

			MPMCQueue(int max_processes, size_type queue_size)
				: m_queue(
					  detail::MakeAndInitialize<lockfree::MPMCQueue<T>>(max_processes, queue_size))
			{
			}

			auto TryPush(int pid, const value_type& val) noexcept -> bool
			{
				return m_queue->TryPush(pid, val);
			}

			auto TryPop(int pid) noexcept -> std::optional<value_type>
			{
				return m_queue->TryPop(pid);
			}

			// Use this variant to avoid need to double copy.
			auto TryPop(int pid, value_type& outval) noexcept -> bool
			{
				return m_queue->TryPop(pid, outval);
			}

			auto IsEmpty() noexcept -> bool { return m_queue->IsEmpty(); }

			auto IsFull() noexcept -> bool { return m_queue->IsFull(); }

		private:
			std::shared_ptr<lockfree::MPMCQueue<T>> m_queue;
		};
	}
}