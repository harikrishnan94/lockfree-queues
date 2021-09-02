#pragma once

#include <boost/preprocessor.hpp>
#include <functional>


namespace lockfree::detail
{
	template <typename Function>
	class
		ScopeExitImpl // NOLINT(hicpp-special-member-functions,cppcoreguidelines-special-member-functions)
	{
	public:
		explicit ScopeExitImpl(Function func) : func(func) {}

		~ScopeExitImpl() { std::invoke(func); }

	private:
		Function func;
	};
}

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define SCOPE_EXIT(lambda)                                                                         \
	auto BOOST_PP_CAT(se, __COUNTER__) = lockfree::detail::ScopeExitImpl(lambda)
