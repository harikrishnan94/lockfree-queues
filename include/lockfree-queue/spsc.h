#pragma once

#include <boost/align/align_up.hpp>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <string_view>

#include "lockfree-queue/detail/defs.h"
#include "lockfree-queue/detail/ringbuf.h"


namespace lockfree
{
	class alignas(detail::CACHELINESIZE) SPSCQueueAny
	{
	public:
		using size_type = std::size_t;

		static auto CalculateSize(size_type queue_size) noexcept -> size_type
		{
			return boost::alignment::align_up(
				sizeof(SPSCQueueAny) + queue_size, alignof(SPSCQueueAny));
		}

		static auto Initialize(void* queue_ptr, size_type queue_size) noexcept -> SPSCQueueAny*
		{
			// NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
			return new (static_cast<SPSCQueueAny*>(queue_ptr)) SPSCQueueAny(queue_size);
		}

		auto TryPush(const void* elem, size_type elemsize) noexcept -> bool
		{
			auto head = detail::load_acquire(m_head);
			auto tail = detail::load_acquire(m_tail);

			if (!is_full(elemsize + sizeof(size_type), head, tail))
			{
				detail::copy_elem_into_ringbuf(
					get_queue_data(), m_queue_size, head, elem, elemsize);

				detail::store_release(m_head, head + sizeof(size_type) + elemsize);
				return true;
			}

			return false;
		}

		auto GetNextElementSize() noexcept -> std::optional<size_type>
		{
			if (auto elem = GetNextElement())
				return elem->size;
			return {};
		}

		// `elem` must be allocated to atleast `min(req_elemsize, GetNextElementSize())` bytes
		auto TryPop(void* elem, size_type req_elemsize) noexcept -> bool
		{
			auto head = detail::load_acquire(m_head);
			auto tail = detail::load_acquire(m_tail);

			assert(tail <= head);
			if (tail < head)
			{
				size_type elemsize;

				detail::copy_out_of_ringbuf(
					get_queue_data(), m_queue_size, tail, &elemsize, sizeof(size_type));
				Pop(tail + sizeof(size_type), elemsize, elem, req_elemsize);
				return true;
			}

			return false;
		}

		auto TryPop(void* elem) noexcept -> bool { return TryPop(elem, m_queue_size); }

		// `elem` must be allocated to atleast `min(req_elemsize, GetNextElementSize())` bytes
		auto TryPeek(void* elem, size_type req_elemsize) noexcept -> bool
		{
			auto head = detail::load_acquire(m_head);
			auto tail = detail::load_acquire(m_tail);

			assert(tail <= head);
			if (tail < head)
			{
				size_type elemsize;

				detail::copy_out_of_ringbuf(
					get_queue_data(), m_queue_size, tail, &elemsize, sizeof(size_type));
				detail::copy_out_of_ringbuf(
					get_queue_data(), m_queue_size, tail, elem, std::min(req_elemsize, elemsize));
				return true;
			}

			return false;
		}

		auto TryPeek(void* elem) noexcept -> bool { return TryPeek(elem, m_queue_size); }


		[[nodiscard]] auto IsFull() const noexcept -> bool
		{
			return is_full(sizeof(size_type) + 1);
		}

		[[nodiscard]] auto IsEmpty() const noexcept -> bool { return is_empty(); }

	private:
		struct ElemInfo
		{
			size_type size;
			size_type pos;
			int cpu = {};
		};

		explicit SPSCQueueAny(size_type queue_size) noexcept : m_queue_size(queue_size) {}

		friend class MPSCPCQueueAny;

		auto GetNextElement() noexcept -> std::optional<ElemInfo>
		{
			auto head = detail::load_acquire(m_head);
			auto tail = detail::load_acquire(m_tail);

			assert(tail <= head);
			if (tail < head)
			{
				size_type elemsize;

				detail::copy_out_of_ringbuf(
					get_queue_data(), m_queue_size, tail, &elemsize, sizeof(size_type));
				return ElemInfo{ elemsize, tail + sizeof(size_type) };
			}

			return {};
		}

		void Pop(size_type tail, size_type elemsize, void* elem, size_type req_elemsize) noexcept
		{
			detail::copy_out_of_ringbuf(
				get_queue_data(), m_queue_size, tail, elem, std::min(req_elemsize, elemsize));
			detail::store_release(m_tail, tail + elemsize);
		}

		[[nodiscard]] auto is_full(size_type elemsize) const noexcept -> bool
		{
			return is_full(elemsize, detail::load_acquire(m_head), detail::load_acquire(m_tail));
		}
		[[nodiscard]] auto is_full(
			size_type elemsize, size_type head, size_type tail) const noexcept -> bool
		{
			return head + elemsize - 1 - tail >= m_queue_size;
		}

		[[nodiscard]] auto is_empty() const noexcept -> bool
		{
			auto head = detail::load_acquire(m_head);
			auto tail = detail::load_acquire(m_tail);

			assert(tail <= head);
			return tail == head;
		}


		auto get_queue_data() noexcept -> char*
		{
			auto* p = reinterpret_cast<char*>(this) + sizeof(SPSCQueueAny);
			return static_cast<char*>(boost::alignment::align_up(p, alignof(SPSCQueueAny)));
		}
		[[nodiscard]] auto get_queue_data() const noexcept -> const char*
		{
			// NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
			return const_cast<SPSCQueueAny*>(this)->get_queue_data();
		}

		const size_type m_queue_size;

		alignas(detail::CACHELINESIZE) std::atomic<size_type> m_head = 0;
		alignas(detail::CACHELINESIZE) std::atomic<size_type> m_tail = 0;
	};

	static_assert(std::is_trivially_copyable_v<SPSCQueueAny>);

	template <typename T> class SPSCQueue
	{
		static_assert(std::is_trivial_v<T>, "Type must be trivial to be store inside queue");

	public:
		using size_type = std::size_t;
		using value_type = T;

		static auto GetAlignment() noexcept -> size_type
		{
			return std::max(alignof(SPSCQueue), alignof(value_type));
		}

		static auto CalculateSize(size_type elemcount) noexcept -> size_type
		{
			return boost::alignment::align_up(
				sizeof(SPSCQueue) + elemcount * sizeof(value_type), GetAlignment());
		}

		static auto Initialize(void* queue_ptr, size_type elemcount) noexcept -> SPSCQueue*
		{
			// NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
			return new (static_cast<SPSCQueue*>(queue_ptr)) SPSCQueue(elemcount);
		}

		auto TryPush(const value_type& elem) noexcept -> bool
		{
			auto head = detail::load_acquire(m_head);
			auto tail = detail::load_acquire(m_tail);

			if (!is_full(head, tail))
			{
				get_queue_data()[head] = elem;
				detail::store_release(m_head, head + 1);
				return true;
			}

			return false;
		}

		// To avoid double copy
		auto TryPop(value_type& elem) noexcept -> bool
		{
			auto head = detail::load_acquire(m_head);
			auto tail = detail::load_acquire(m_tail);

			assert(tail <= head);
			if (tail < head)
			{
				elem = get_queue_data()[tail];
				detail::store_release(m_tail, tail + 1);
				return true;
			}

			return false;
		}

		auto TryPop() noexcept -> std::optional<value_type>
		{
			value_type elem;
			if (TryPop(elem))
				return elem;
			return {};
		}

		// To avoid double copy
		auto TryPeek(value_type& elem) noexcept -> bool
		{
			auto head = detail::load_acquire(m_head);
			auto tail = detail::load_acquire(m_tail);

			assert(tail <= head);
			if (tail < head)
			{
				elem = get_queue_data()[tail];
				return true;
			}

			return false;
		}

		auto TryPeek() noexcept -> std::optional<value_type>
		{
			value_type elem;
			if (TryPeek(elem))
				return elem;
			return {};
		}

		[[nodiscard]] auto IsFull() const noexcept -> bool
		{
			return is_full(sizeof(size_type) + 1);
		}

		[[nodiscard]] auto IsEmpty() const noexcept -> bool { return is_empty(); }

	private:
		explicit SPSCQueue(size_type elemcount) noexcept : m_queue_size(elemcount) {}


		[[nodiscard]] auto is_full() const noexcept -> bool
		{
			return is_full(detail::load_acquire(m_head), detail::load_acquire(m_tail));
		}
		[[nodiscard]] auto is_full(size_type head, size_type tail) const noexcept -> bool
		{
			return head - tail >= m_queue_size;
		}

		[[nodiscard]] auto is_empty() const noexcept -> bool
		{
			auto head = detail::load_acquire(m_head);
			auto tail = detail::load_acquire(m_tail);

			assert(tail <= head);
			return tail == head;
		}


		auto get_queue_data() noexcept -> value_type*
		{
			auto* p = reinterpret_cast<char*>(this) + sizeof(SPSCQueue);
			return static_cast<value_type*>(boost::alignment::align_up(p, alignof(SPSCQueue)));
		}
		[[nodiscard]] auto get_queue_data() const noexcept -> const value_type*
		{
			// NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
			return const_cast<SPSCQueue*>(this)->get_queue_data();
		}

		const size_type m_queue_size;

		alignas(detail::CACHELINESIZE) std::atomic<size_type> m_head = 0;
		alignas(detail::CACHELINESIZE) std::atomic<size_type> m_tail = 0;
	};

	namespace thread
	{
		class SPSCQueueAny
		{
		public:
			using size_type = lockfree::SPSCQueueAny::size_type;

			explicit SPSCQueueAny(size_type queue_size)
				: m_queue(detail::MakeAndInitialize<lockfree::SPSCQueueAny>(queue_size))
			{
			}

			auto TryPush(const void* elem, size_type elemsize) noexcept -> bool
			{
				return m_queue->TryPush(elem, elemsize);
			}


			auto TryPush(std::string_view elem) noexcept -> bool
			{
				return m_queue->TryPush(elem.data(), elem.length());
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
			std::shared_ptr<lockfree::SPSCQueueAny> m_queue;
		};

		template <typename T> class SPSCQueue
		{
		public:
			using size_type = typename lockfree::SPSCQueue<T>::size_type;
			using value_type = typename lockfree::SPSCQueue<T>::value_type;

			explicit SPSCQueue(size_type elem_count)
				: m_queue(detail::MakeAndInitialize<lockfree::SPSCQueue<T>>(elem_count))
			{
			}

			auto TryPush(const value_type& val) noexcept -> bool { return m_queue->TryPush(val); }

			auto TryPop() noexcept -> std::optional<value_type> { return m_queue->TryPop(); }
			auto TryPeek() noexcept -> std::optional<value_type> { return m_queue->TryPeek(); }

			// Use this variant to avoid need to double copy.
			auto TryPop(value_type& outval) noexcept -> bool { return m_queue->TryPop(outval); }

			// Use this variant to avoid need to double copy.
			auto TryPeek(value_type& outval) noexcept -> bool { return m_queue->TryPeek(outval); }

			auto IsEmpty() noexcept -> bool { return m_queue->IsEmpty(); }

			auto IsFull() noexcept -> bool { return m_queue->IsFull(); }

		private:
			std::shared_ptr<lockfree::SPSCQueue<T>> m_queue;
		};
	}
}