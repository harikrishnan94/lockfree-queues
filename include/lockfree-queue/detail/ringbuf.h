#pragma once

#include <algorithm>
#include <cstring>

namespace lockfree::detail
{
	template <typename size_type>
	void copy_out_of_ringbuf(
		const char* rb_base, size_type rb_sz, size_type rb_tail, void* dst, size_type size) noexcept
	{
		rb_tail %= rb_sz;
		const auto len = std::min(size, rb_sz - rb_tail);

		std::memcpy(dst, rb_base + rb_tail, len);
		if (len < size)
			std::memcpy(static_cast<char*>(dst) + len, rb_base, size - len);
	}


	template <typename size_type>
	void copy_into_ringbuf(
		char* rb_base, size_type rb_sz, size_type rb_head, const void* src, size_type size) noexcept
	{
		rb_head %= rb_sz;
		const auto len = std::min(size, rb_sz - rb_head);

		std::memcpy(rb_base + rb_head, src, len);
		if (len < size)
			std::memcpy(rb_base, static_cast<const char*>(src) + len, size - len);
	}

	template <typename size_type>
	void copy_elem_into_ringbuf(char* rb_base, size_type rb_sz, size_type rb_head, const void* elem,
		size_type size) noexcept
	{
		copy_into_ringbuf(rb_base, rb_sz, rb_head, &size, sizeof(size));
		copy_into_ringbuf(rb_base, rb_sz, rb_head + sizeof(size), elem, size);
	}
}