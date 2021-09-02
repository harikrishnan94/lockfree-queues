#pragma once

#include <thread>

#include "lockfree-queue/spsc.h"


namespace lockfree
{
	class alignas(detail::CACHELINESIZE) MPSCPCQueueAny
	{
	public:
		using size_type = std::size_t;

		// Check if MPSC-PC requirements are supported by the system.
		// At present, kernel must support
		// `rseq`(https://github.com/torvalds/linux/blob/master/kernel/rseq.c#L26) syscall.
		static auto Available() noexcept -> bool;

		static auto CalculateSize(size_type per_cpu_queue_size) noexcept -> size_type
		{
			auto size = sizeof(MPSCPCQueueAny) +
						SPSCQueueAny::CalculateSize(per_cpu_queue_size) * NUM_CORES;
			return size;
		}

		static auto Initialize(void* queue_ptr, size_type per_cpu_queue_size) noexcept
			-> MPSCPCQueueAny*
		{
			// NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
			return new (static_cast<MPSCPCQueueAny*>(queue_ptr)) MPSCPCQueueAny(per_cpu_queue_size);
		}


		// Push `elem` into current cpu's queue.
		// Return's false if queue belonging to current cpu is full.
		auto TryPush(const void* elem, size_type elemsize) noexcept -> bool;

		auto GetNextElementSize() noexcept -> std::optional<size_type>
		{
			if (m_current_fetch_elem)
				return m_current_fetch_elem->size;

			for (int i = 0; i < NUM_CORES; i++)
			{
				if (m_next_poll_cpu == NUM_CORES)
					m_next_poll_cpu = 0;

				auto cpu = m_next_poll_cpu++;
				if (auto res = get_queue(this, cpu).GetNextElement())
				{
					res->cpu = cpu;
					m_current_fetch_elem = res;
					return res->size;
				}
			}

			return {};
		}

		// `elem` must be allocated to atleast `min(elemsize, GetNextElementSize())` bytes
		auto TryPop(void* elem, size_type req_elemsize) noexcept -> bool
		{
			if (auto size = GetNextElementSize())
			{
				get_queue(this, m_current_fetch_elem->cpu)
					.Pop(m_current_fetch_elem->pos, m_current_fetch_elem->size, elem, req_elemsize);
				m_current_fetch_elem.reset();
				return true;
			}

			return false;
		}

		// `elem` must be allocated to atleast `GetNextElementSize` bytes
		auto TryPop(void* elem) noexcept -> bool { return TryPop(elem, m_percpu_queue_size); }

		// Check if Current cpu's queue is empty.
		// XXX: Result should only be used as hint, as the current thread might have been be
		// migrated to different cpu afterwards.
		[[nodiscard]] auto IsEmpty() const noexcept -> bool;

		// Check if current cpu's queue is full.
		// XXX: Result should only be used as hint, as the current thread might have been be
		// migrated to different cpu afterwards.
		[[nodiscard]] auto IsFull() const noexcept -> bool;

	private:
		static auto get_queue(MPSCPCQueueAny* queue, int cpuid) noexcept -> SPSCQueueAny&
		{
			auto* p = reinterpret_cast<char*>(queue) + sizeof(MPSCPCQueueAny);
			return *reinterpret_cast<SPSCQueueAny*>(p + queue->m_percpu_queue_size * cpuid);
		}
		static auto get_queue(const MPSCPCQueueAny* queue, int cpuid) noexcept
			-> const SPSCQueueAny&
		{
			const auto* p = reinterpret_cast<const char*>(queue) + sizeof(MPSCPCQueueAny);
			return *reinterpret_cast<const SPSCQueueAny*>(p + queue->m_percpu_queue_size * cpuid);
		}


		explicit MPSCPCQueueAny(size_type per_cpu_ring_buf_size) noexcept
			: m_per_cpu_ring_buf_size(per_cpu_ring_buf_size),
			  m_percpu_queue_size(SPSCQueueAny::CalculateSize(per_cpu_ring_buf_size))
		{
			for (int i = 0; i < NUM_CORES; i++)
			{
				SPSCQueueAny::Initialize(&get_queue(this, i), per_cpu_ring_buf_size);
			}
		}


		[[nodiscard]] auto is_full(int cpuid) const noexcept -> bool
		{
			return get_queue(this, cpuid).IsFull();
		}
		[[nodiscard]] auto is_empty(int cpuid) const noexcept -> bool
		{
			return get_queue(this, cpuid).IsEmpty();
		}


		static inline const int NUM_CORES = int(std::thread::hardware_concurrency());

		const size_type m_per_cpu_ring_buf_size;
		const size_type m_percpu_queue_size;
		int m_next_poll_cpu = 0;
		std::optional<SPSCQueueAny::ElemInfo> m_current_fetch_elem = {};
	};

	static_assert(std::is_trivially_copyable_v<MPSCPCQueueAny>);

	namespace thread
	{
		class MPSCPCQueueAny
		{
		public:
			using size_type = std::size_t;

			explicit MPSCPCQueueAny(size_type queue_size)
				: m_queue(detail::MakeAndInitialize<lockfree::MPSCPCQueueAny>(queue_size))
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

			auto IsEmpty() noexcept -> bool { return m_queue->IsEmpty(); }

			auto IsFull() noexcept -> bool { return m_queue->IsFull(); }

		private:
			std::shared_ptr<lockfree::MPSCPCQueueAny> m_queue;
		};
	}
}