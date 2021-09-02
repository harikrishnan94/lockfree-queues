#pragma once

#include <atomic>
#include <boost/align/aligned_alloc.hpp>
#include <functional>
#include <memory>

namespace lockfree::detail
{
	static constexpr std::size_t CACHELINESIZE = 64;

	template <typename T> static inline void store_release(std::atomic<T>& aval, T val) noexcept
	{
		aval.store(val, std::memory_order_release);
	}

	template <typename T> static inline auto load_acquire(const std::atomic<T>& aval) noexcept -> T
	{
		return aval.load(std::memory_order_acquire);
	}

	template <typename T, typename... InitArgs>
	inline auto MakeAndInitialize(InitArgs&&... initargs) -> std::shared_ptr<T>
	{
		const auto size = T::CalculateSize(std::forward<InitArgs>(initargs)...);

		auto deleter = [](void* p) { boost::alignment::aligned_free(p); };

		std::unique_ptr<void, decltype(deleter)> uninit_mem(
			boost::alignment::aligned_alloc(alignof(T), size), deleter);
		std::unique_ptr<T, decltype(deleter)> mem(
			T::Initialize(uninit_mem.get(), std::forward<InitArgs>(initargs)...), deleter);
		(void)uninit_mem.release(); // `mem` is now the sole owner.

		return mem;
	}
}