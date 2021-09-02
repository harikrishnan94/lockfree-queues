#pragma once

#include <algorithm>
#include <immintrin.h>
#include <thread>

namespace lockfree
{
	template <unsigned Step, bool AlwaysBusy> class Backoff
	{
	public:
		explicit Backoff(
			unsigned start_delay =
				10, // NOLINT(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
			unsigned max_delay =
				1000) // NOLINT(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
			: m_cur_delay(std::max(start_delay, 1U)), m_max_delay(max_delay)
		{
		}

		void operator()()
		{
			constexpr auto BUSY_WAIT_LIMIT = 32;

			if (AlwaysBusy || m_cur_delay < BUSY_WAIT_LIMIT)
			{
				for (unsigned i = 0; i < m_cur_delay; i++)
				{
					_mm_pause();
				}
			}
			else
			{
				std::this_thread::sleep_for(std::chrono::microseconds(m_cur_delay));
			}

			m_cur_delay = std::min(m_cur_delay * 2, m_max_delay);
		}

	private:
		unsigned m_cur_delay;
		unsigned m_max_delay;
	};

	using ConstantBackoff = Backoff<1, false>;
	using ExponentialBackoff = Backoff<2, false>;
	using ExponentialBackoffBusy = Backoff<2, true>;
}