#include <atomic>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/interprocess/sync/interprocess_condition.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <chrono>
#include <memory>
#include <mutex>

#include "lockfree-queue/detail/defs.h"
#include "waitevent.h"

namespace lockfree
{
	using namespace std::chrono;
	using namespace boost::interprocess;

	// https://lists.boost.org/boost-commit/2009/04/15209.php
	template <typename Clock, typename Duration>
	static auto convert_to_ptime(const time_point<Clock, Duration>& from)
		-> boost::posix_time::ptime
	{
		using duration_t = nanoseconds;
		auto d = duration_cast<duration_t>(from.time_since_epoch()).count();
		auto sec = d / std::nano::den;
		auto to_subseconds = [&] {
#ifdef BOOST_DATE_TIME_HAS_NANOSECONDS
			auto nsec = d % std::nano::den;
			return boost::posix_time::nanoseconds(nsec);
#else
			auto usec = d % std::micro::den;
			return boost::posix_time::microseconds(usec);
#endif
		};

		return boost::posix_time::from_time_t(0) + boost::posix_time::seconds(sec) +
			   to_subseconds();
	}

	class WaitEvent::Impl : public WaitEvent
	{
	public:
		Impl() = default;

		[[nodiscard]] auto GetNumWaiters() const noexcept -> int
		{
			return m_waiters.load(std::memory_order_acquire);
		}

		void Wait()
		{
			std::unique_lock lock(m_mtx);
			m_cv.wait(lock);
		}

		auto WaitUntil(const time_point<system_clock, microseconds>& time) -> we_status
		{
			std::unique_lock lock(m_mtx);
			return m_cv.timed_wait(lock, convert_to_ptime(time)) ? we_status::no_timeout
																 : we_status::timeout;
		}

		void WakeupOneWaiter()
		{
			if (GetNumWaiters() != 0)
			{
				std::unique_lock lock(m_mtx);
				m_cv.notify_one();
			}
		}

		void WakeupAllWaiters()
		{
			if (GetNumWaiters() != 0)
			{
				std::unique_lock lock(m_mtx);
				m_cv.notify_all();
			}
		}

		void IncrementWaiters() noexcept { ++m_waiters; }
		void DecrementWaiters() noexcept { --m_waiters; }

	private:
		alignas(detail::CACHELINESIZE) std::atomic<int> m_waiters = 0;
		interprocess_mutex m_mtx = {};
		interprocess_condition m_cv = {};
	};

	auto WaitEvent::GetAlignment() noexcept -> std::size_t { return alignof(Impl); }
	auto WaitEvent::CalculateSize() noexcept -> std::size_t { return sizeof(Impl); }

	auto WaitEvent::Initialize(void* we_data) -> WaitEvent* { return new (we_data) Impl{}; }
	void WaitEvent::Destroy(WaitEvent* we_data) noexcept { std::destroy_at(we_data->get_impl()); }

	auto WaitEvent::GetNumWaiters() const noexcept -> int { return get_impl()->GetNumWaiters(); }

	void WaitEvent::WakeupOneWaiter() { get_impl()->WakeupOneWaiter(); }

	void WaitEvent::WakeupAllWaiters() { get_impl()->WakeupAllWaiters(); }

	void WaitEvent::increment_waitercount() noexcept { get_impl()->IncrementWaiters(); }
	void WaitEvent::decrement_waitercount() noexcept { get_impl()->DecrementWaiters(); }

	void WaitEvent::wait() { get_impl()->Wait(); }

	auto WaitEvent::wait_until(const time_point<system_clock, microseconds>& time) -> we_status
	{
		return get_impl()->WaitUntil(time);
	}

	auto WaitEvent::get_impl() const noexcept -> const Impl*
	{
		return static_cast<const Impl*>(this); // NOLINT
	}
	auto WaitEvent::get_impl() noexcept -> Impl* { return static_cast<Impl*>(this); } // NOLINT
}