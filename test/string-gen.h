#pragma once

#include <charconv>
#include <doctest/doctest.h>
#include <random>
#include <string>
#include <string_view>

class StringGen
{
public:
	static constexpr size_t AVGLEN = 1024;

	auto operator()() -> std::string_view
	{
		const auto len = AVGLEN + (ptrdiff_t(AVGLEN) * m_var_dist(m_gen)) / 100;
		std::hash<std::string_view> hasher;

		m_data.clear();
		for (size_t i = 0; i < len; i++)
		{
			m_data += ALPHANUM[m_char_dist(m_gen)];
		}

		auto hash = hasher(m_data);

		m_data += '-';
		m_data += std::to_string(hash);
		return m_data;
	}

	static auto Verify(std::string_view data) -> bool
	{
		const auto hyp = data.find('-');
		if (hyp == std::string_view::npos || hyp == data.length() - 1)
			return false;

		auto hashstr = data.substr(hyp + 1);
		size_t hash;

		if (auto res = std::from_chars(hashstr.data(), hashstr.data() + hashstr.length(), hash);
			res.ec != std::errc())
		{
			return false;
		}

		auto basestr = data.substr(0, hyp);
		auto strhash = std::hash<std::string_view>{}(basestr);
		return strhash == hash;
	}

private:
	static constexpr std::string_view ALPHANUM =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqrstuvwxyz";
	static constexpr auto VAR_PCT = 10;

	std::string m_data;

	std::mt19937_64 m_gen{ std::random_device{}() };
	std::uniform_int_distribution<int> m_var_dist{ -VAR_PCT, VAR_PCT };
	std::uniform_int_distribution<size_t> m_char_dist{ 0, ALPHANUM.length() - 1 };
};

template <typename Queue> void pop(Queue&& queue_, size_t count)
{
	std::string data;
	auto&& queue = std::forward<Queue>(queue_);

	for (size_t i = 0; i < count; i++)
	{
		while (true)
		{
			if (auto size = queue.GetNextElementSize())
			{
				data.clear();
				data.resize(*size + 1);
				queue.TryPop(data.data());

				REQUIRE(StringGen::Verify(data));
				break;
			}
		}
	}
}
