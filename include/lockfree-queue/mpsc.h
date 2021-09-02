#pragma once

#include <boost/align/align_up.hpp>
#include <boost/align/aligned_alloc.hpp>
#include <cstddef>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <string_view>


#include "lockfree-queue/backoff.h"
#include "lockfree-queue/detail/defs.h"
#include "lockfree-queue/detail/ringbuf.h"
#include "lockfree-queue/detail/scopeexit.h"


namespace lockfree
{
	class alignas(detail::CACHELINESIZE) MPSCQueueAny
	{
	public:
		using size_type = std::size_t;

		static auto CalculateSize(int max_processes, size_type queue_size) noexcept -> size_type
		{
			auto size = sizeof(MPSCQueueAny);

			size = boost::alignment::align_up(size, alignof(ThreadPos));
			size += sizeof(ThreadPos) * max_processes;

			return boost::alignment::align_up(size, alignof(MPSCQueueAny)) + queue_size;
		}

		static auto Initialize(void* queue_ptr, int max_processes, size_type queue_size) noexcept
			-> MPSCQueueAny*
		{
			// NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
			return new (static_cast<MPSCQueueAny*>(queue_ptr))
				MPSCQueueAny(max_processes, queue_size);
		}


		auto TryPush(int pid, const void* elem, size_type elemsize) noexcept -> bool
		{
			SCOPE_EXIT([&] { detail::store_release(get_tpos_data()[pid].head, INVALID_Q_POS); });

			if (auto head = reserve_head_to_produce(pid, elemsize + sizeof(size_type)))
			{
				detail::copy_elem_into_ringbuf(
					get_queue_data(), m_queue_size, *head, elem, elemsize);
				return true;
			}

			return false;
		}


		auto GetNextElementSize() noexcept -> std::optional<size_type>
		{
			return get_next_elem_size<true>();
		}

		// `elem` must be allocated to atleast `min(elemsize, GetNextElementSize())` bytes
		auto TryPop(void* elem, size_type req_elemsize) noexcept -> bool
		{
			auto tail = detail::load_acquire(m_tail);

			if (auto elemsize = get_next_elem_size())
			{
				detail::copy_out_of_ringbuf(get_queue_data(), m_queue_size,
					tail + sizeof(size_type), elem, std::min(req_elemsize, *elemsize));
				detail::store_release(m_tail, tail + *elemsize + sizeof(size_type));
				return true;
			}

			return false;
		}

		// `elem` must be allocated to atleast `min(elemsize, GetNextElementSize())` bytes
		auto TryPeek(void* elem, size_type req_elemsize) noexcept -> bool
		{
			auto tail = detail::load_acquire(m_tail);

			if (auto elemsize = get_next_elem_size())
			{
				detail::copy_out_of_ringbuf(get_queue_data(), m_queue_size,
					tail + sizeof(size_type), elem, std::min(req_elemsize, *elemsize));
				return true;
			}

			return false;
		}

		// `elem` must be allocated to atleast `GetNextElementSize` bytes
		auto TryPop(void* elem) noexcept -> bool { return TryPop(elem, m_queue_size); }

		// `elem` must be allocated to atleast `GetNextElementSize` bytes
		auto TryPeek(void* elem) noexcept -> bool { return TryPeek(elem, m_queue_size); }

		auto IsEmpty() noexcept -> bool
		{
			size_type last_head;
			auto is_empty = [&] {
				return lockfree::MPSCQueueAny::is_empty(
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
			return is_full(detail::load_acquire(m_head), detail::load_acquire(m_tail), 1);
		}

	private:
		static constexpr auto INVALID_Q_POS = std::numeric_limits<size_type>::max();

		struct alignas(detail::CACHELINESIZE) ThreadPos
		{
			std::atomic<size_type> head = INVALID_Q_POS;
		};

		MPSCQueueAny(int max_processes, size_type queue_size) noexcept
			: m_max_processes(max_processes), m_queue_size(queue_size)
		{
			auto* tpos = get_tpos_data();
			for (int i = 0; i < max_processes; i++)
				new (&tpos[i]) ThreadPos{};
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


		auto reserve_head_to_produce(int pid, size_type elemsize) noexcept
			-> std::optional<size_type>
		{
			auto head = detail::load_acquire(m_head);
			auto last_tail = detail::load_acquire(m_tail);
			auto* tpos = get_tpos_data();
			ExponentialBackoff backoff;

			while (!is_full(head, last_tail, elemsize))
			{
				detail::store_release(tpos[pid].head, head);

				if (m_head.compare_exchange_strong(head, head + elemsize))
					return head;

				backoff();

				head = detail::load_acquire(m_head);
				last_tail = detail::load_acquire(m_tail);
			}

			return {};
		}

		template <bool TryAgain = true> auto get_next_elem_size() -> std::optional<size_type>
		{
			auto last_head = detail::load_acquire(m_last_head);
			auto tail = detail::load_acquire(m_tail);

			if (!is_empty(last_head, tail))
			{
				size_type elemsize;

				detail::copy_out_of_ringbuf(
					get_queue_data(), m_queue_size, tail, &elemsize, sizeof(size_type));
				return elemsize;
			}

			if constexpr (TryAgain)
			{
				update_last_head(last_head);
				return get_next_elem_size<false>();
			}

			return {}; // NOLINT(readability-misleading-indentation)
		}


		[[nodiscard]] auto is_full(
			size_type head, size_type tail, size_type elemsize) const noexcept -> bool
		{
			return head + elemsize - 1 >= tail + m_queue_size;
		}
		static auto is_empty(size_type head, size_type tail) noexcept -> bool
		{
			return tail >= head;
		}


		auto get_tpos_data() noexcept -> ThreadPos*
		{
			auto* p = reinterpret_cast<char*>(this);
			return static_cast<ThreadPos*>(
				boost::alignment::align_up(p + sizeof(MPSCQueueAny), alignof(ThreadPos)));
		}
		[[nodiscard]] auto get_tpos_data() const noexcept -> const ThreadPos*
		{
			// NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
			return const_cast<MPSCQueueAny*>(this)->get_tpos_data();
		}

		auto get_queue_data() noexcept -> char*
		{
			auto* p = reinterpret_cast<char*>(get_tpos_data());
			return static_cast<char*>(boost::alignment::align_up(
				p + sizeof(ThreadPos) * m_max_processes, detail::CACHELINESIZE));
		}
		[[nodiscard]] auto get_queue_data() const noexcept -> const char*
		{
			// NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
			return const_cast<MPSCQueueAny*>(this)->get_queue_data();
		}


		const int m_max_processes;
		const size_type m_queue_size;

		alignas(detail::CACHELINESIZE) std::atomic<size_type> m_head = 0;
		alignas(detail::CACHELINESIZE) std::atomic<size_type> m_tail = 0;
		alignas(detail::CACHELINESIZE) std::atomic<size_type> m_last_head = 0;
	};

	static_assert(std::is_trivially_copyable_v<MPSCQueueAny>);

	template <typename T> class MPSCQueue
	{
		static_assert(std::is_trivial_v<T>, "Type must be trivial to be store inside queue");

	public:
		using size_type = std::size_t;
		using value_type = T;

		static auto GetAlignment() noexcept -> size_type
		{
			return std::max({ alignof(MPSCQueue), alignof(T), detail::CACHELINESIZE });
		}

		static auto CalculateSize(int max_processes, size_type queue_size) noexcept -> size_type
		{
			auto size = sizeof(MPSCQueue);

			size = boost::alignment::align_up(size, alignof(ThreadPos));
			size += sizeof(ThreadPos) * max_processes;

			return boost::alignment::align_up(size, std::max(detail::CACHELINESIZE, alignof(T))) +
				   queue_size * sizeof(value_type);
		}

		static auto Initialize(void* queue_ptr, int max_processes, size_type queue_size) noexcept
			-> MPSCQueue*
		{
			// NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
			return new (static_cast<MPSCQueue*>(queue_ptr)) MPSCQueue(max_processes, queue_size);
		}

		static void Destroy(MPSCQueue* ptr) noexcept { std::destroy_at(ptr); }

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

		auto TryPop() noexcept -> std::optional<value_type>
		{
			if (auto tail = get_tail())
			{
				SCOPE_EXIT([&] { detail::store_release(m_tail, *tail + 1); });
				return get_queue_data()[*tail % m_queue_size];
			}

			return {};
		}

		// Use this variant to avoid need to double copy.
		auto TryPop(value_type& outval) noexcept -> bool
		{
			if (auto tail = get_tail())
			{
				SCOPE_EXIT([&] { detail::store_release(m_tail, *tail + 1); });
				outval = get_queue_data()[*tail % m_queue_size];
				return true;
			}
			return false;
		}

		auto TryPeek() noexcept -> std::optional<value_type>
		{
			if (auto tail = get_tail())
				return get_queue_data()[*tail % m_queue_size];

			return {};
		}

		// Use this variant to avoid need to double copy.
		auto TryPeek(value_type& outval) noexcept -> bool
		{
			if (auto tail = get_tail())
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
			return is_full(detail::load_acquire(m_head), detail::load_acquire(m_tail));
		}

	private:
		static constexpr auto INVALID_Q_POS = std::numeric_limits<size_type>::max();

		struct alignas(detail::CACHELINESIZE) ThreadPos
		{
			std::atomic<size_type> head = INVALID_Q_POS;
		};

		MPSCQueue(int max_processes, size_type queue_size) noexcept
			: m_max_processes(max_processes), m_queue_size(queue_size)
		{
			auto* tpos = get_tpos_data();
			for (int i = 0; i < max_processes; i++)
				new (&tpos[i]) ThreadPos{};
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


		auto reserve_head_to_produce(int pid) noexcept -> std::optional<size_type>
		{
			auto head = detail::load_acquire(m_head);
			auto last_tail = detail::load_acquire(m_tail);
			auto* tpos = get_tpos_data();
			ExponentialBackoff backoff;

			while (!is_full(head, last_tail))
			{
				detail::store_release(tpos[pid].head, head);

				if (m_head.compare_exchange_strong(head, head + 1))
					return head;

				backoff();

				head = detail::load_acquire(m_head);
				last_tail = detail::load_acquire(m_tail);
			}

			return {};
		}

		template <bool TryAgain = true> auto get_tail() -> std::optional<size_type>
		{
			auto last_head = detail::load_acquire(m_last_head);
			auto tail = detail::load_acquire(m_tail);

			if (!is_empty(last_head, tail))
				return tail;

			if constexpr (TryAgain)
			{
				update_last_head(last_head);
				return get_tail<false>();
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
				boost::alignment::align_up(p + sizeof(MPSCQueue), alignof(ThreadPos)));
		}
		[[nodiscard]] auto get_tpos_data() const noexcept -> const ThreadPos*
		{
			// NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
			return const_cast<MPSCQueue*>(this)->get_tpos_data();
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
			return const_cast<MPSCQueue*>(this)->get_queue_data();
		}


		const int m_max_processes;
		const size_type m_queue_size;

		alignas(detail::CACHELINESIZE) std::atomic<size_type> m_head = 0;
		alignas(detail::CACHELINESIZE) std::atomic<size_type> m_tail = 0;
		alignas(detail::CACHELINESIZE) std::atomic<size_type> m_last_head = 0;
	};

	namespace thread
	{
		class MPSCQueueAny
		{
		public:
			using size_type = std::size_t;

			MPSCQueueAny(int max_processes, size_type queue_size)
				: m_queue(
					  detail::MakeAndInitialize<lockfree::MPSCQueueAny>(max_processes, queue_size))
			{
			}

			auto TryPush(int pid, const void* elem, size_type elemsize) noexcept -> bool
			{
				return m_queue->TryPush(pid, elem, elemsize);
			}


			auto TryPush(int pid, std::string_view elem) noexcept -> bool
			{
				return m_queue->TryPush(pid, elem.data(), elem.length());
			}

			auto GetNextElementSize() noexcept -> std::optional<size_type>
			{
				return m_queue->GetNextElementSize();
			}

			auto TryPop(void* elem) noexcept -> bool { return m_queue->TryPop(elem); }
			auto TryPop(void* elem, size_type req_elemsize) noexcept -> bool
			{
				return m_queue->TryPop(elem, req_elemsize);
			}

			auto TryPeek(void* elem) noexcept -> bool { return m_queue->TryPeek(elem); }
			auto TryPeek(void* elem, size_type req_elemsize) noexcept -> bool
			{
				return m_queue->TryPeek(elem, req_elemsize);
			}

			auto IsEmpty() noexcept -> bool { return m_queue->IsEmpty(); }

			auto IsFull() noexcept -> bool { return m_queue->IsFull(); }

		private:
			std::shared_ptr<lockfree::MPSCQueueAny> m_queue;
		};

		template <typename T> class MPSCQueue
		{
		public:
			using size_type = std::size_t;
			using value_type = T;

			MPSCQueue(int max_processes, size_type queue_size)
				: m_queue(
					  detail::MakeAndInitialize<lockfree::MPSCQueue<T>>(max_processes, queue_size))
			{
			}

			auto TryPush(int pid, const value_type& val) noexcept -> bool
			{
				return m_queue->TryPush(pid, val);
			}

			auto TryPop() noexcept -> std::optional<value_type> { return m_queue->TryPop(); }
			auto TryPeek() noexcept -> std::optional<value_type> { return m_queue->TryPeek(); }

			// Use this variant to avoid need to double copy.
			auto TryPop(value_type& outval) noexcept -> bool { return m_queue->TryPop(outval); }

			// Use this variant to avoid need to double copy.
			auto TryPeek(value_type& outval) noexcept -> bool { return m_queue->TryPeek(outval); }

			auto IsEmpty() noexcept -> bool { return m_queue->IsEmpty(); }

			auto IsFull() noexcept -> bool { return m_queue->IsFull(); }

		private:
			std::shared_ptr<lockfree::MPSCQueue<T>> m_queue;
		};
	}
}