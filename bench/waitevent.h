#pragma once

#include <chrono>
#include <functional>
#include <memory>

#include "lockfree-queue/detail/defs.h"
#include "lockfree-queue/detail/scopeexit.h"

namespace lockfree
{
	enum class we_status
	{
		no_timeout,
		timeout
	};

	class WaitEvent
	{
	public:
		static auto GetAlignment() noexcept -> std::size_t;
		static auto CalculateSize() noexcept -> std::size_t;

		static auto Initialize(void* we_data) -> WaitEvent*;
		static void Destroy(WaitEvent* we_data) noexcept;

		[[nodiscard]] auto GetNumWaiters() const noexcept -> int;

		void Wait()
		{
			Wait([] { return false; });
		}

		template <typename Predicate> void Wait(Predicate&& pred)
		{
			if (!std::invoke(pred))
			{
				increment_waitercount();
				SCOPE_EXIT([&] { decrement_waitercount(); });

				while (!std::invoke(pred))
				{
					wait();
				}
			}
		}

		template <typename Clock, typename Duration>
		auto WaitUntil(const std::chrono::time_point<Clock, Duration>& time) -> we_status
		{
			return wait_until(time, [] { return false; });
		}

		template <typename Clock, typename Duration, typename Predicate>
		auto WaitUntil(const std::chrono::time_point<Clock, Duration>& time, Predicate&& pred)
			-> we_status
		{
			bool is_pred_satisfied = false;
			auto predicate = [&] { return is_pred_satisfied = std::invoke(pred); };

			if (!predicate())
			{
				using namespace std::chrono;
				auto clk_now = Clock::now();
				auto sclk_now = DefClock::now();
				auto stime = time_point_cast<microseconds>(sclk_now + time - clk_now);

				increment_waitercount();
				SCOPE_EXIT([&] { decrement_waitercount(); });

				while (!predicate() && Clock::now() < time)
				{
					if (wait_until(stime) == we_status::timeout)
					{
						// We got a timeout when measured against DefClock but
						// we need to check against the caller-supplied clock
						// to tell whether we should return a timeout.
						if (Clock::now() > time)
							return we_status::timeout;
					}
				}
			}

			return is_pred_satisfied ? we_status::no_timeout : we_status::timeout;
		}

		template <typename Rep, typename Period>
		auto WaitFor(const std::chrono::duration<Rep, Period>& duration) -> we_status
		{
			return WaitFor(duration, [] { return false; });
		}

		template <typename Rep, typename Period, typename Predicate>
		auto WaitFor(const std::chrono::duration<Rep, Period>& duration, Predicate&& pred)
			-> we_status
		{
			return WaitUntil(DefClock::now() + duration, std::forward<Predicate>(pred));
		}

		void WakeupOneWaiter();

		void WakeupAllWaiters();

	protected:
		WaitEvent() = default;

	private:
		class Impl;
		using DefClock = std::chrono::steady_clock;

		void increment_waitercount() noexcept;
		void decrement_waitercount() noexcept;
		void wait();
		auto wait_until(
			const std::chrono::time_point<std::chrono::system_clock, std::chrono::microseconds>&
				time) -> we_status;

		[[nodiscard]] auto get_impl() const noexcept -> const Impl*;
		auto get_impl() noexcept -> Impl*;
	};

	namespace thread
	{
		class WaitEvent
		{
		public:
			[[nodiscard]] auto GetNumWaiters() const noexcept -> int
			{
				return m_we->GetNumWaiters();
			}

			void Wait() { m_we->Wait(); }

			template <typename Predicate> void Wait(Predicate&& pred)
			{
				m_we->Wait(std::forward<Predicate>(pred));
			}

			template <typename Clock, typename Duration>
			auto WaitUntil(const std::chrono::time_point<Clock, Duration>& time) -> we_status
			{
				return m_we->WaitUntil(time);
			}

			template <typename Clock, typename Duration, typename Predicate>
			auto WaitUntil(const std::chrono::time_point<Clock, Duration>& time, Predicate&& pred)
				-> we_status
			{
				return m_we->WaitUntil(time, std::forward<Predicate>(pred));
			}

			template <typename Rep, typename Period>
			auto WaitFor(const std::chrono::duration<Rep, Period>& duration) -> we_status
			{
				return m_we->WaitFor(duration);
			}

			template <typename Rep, typename Period, typename Predicate>
			auto WaitFor(const std::chrono::duration<Rep, Period>& duration, Predicate&& pred)
				-> we_status
			{
				return m_we->WaitFor(duration, std::forward<Predicate>(pred));
			}

			void WakeupOneWaiter() { m_we->WakeupOneWaiter(); }

			void WakeupAllWaiters() { m_we->WakeupAllWaiters(); }

		private:
			std::shared_ptr<lockfree::WaitEvent> m_we =
				detail::MakeAndInitialize<lockfree::WaitEvent>();
		};
	}
}