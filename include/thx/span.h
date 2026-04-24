/*
 *  Created by LuckyNeko on 24/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include <cstddef>

namespace thx
{
	// ABI-stable non-owning contiguous range.
	//
	// std::span (C++20) and gsl::span have implementation-defined layouts.
	// thx::Span<T> is guaranteed to be { T* data, size_t size } and is safe
	// to pass through virtual method signatures across plugin boundaries.
	template <typename T>
	class Span
	{
	public:
		constexpr Span() noexcept : data_(nullptr), size_(0) {}

		constexpr Span(T* data, std::size_t size) noexcept : data_(data), size_(size) {}

		template <std::size_t N>
		constexpr Span(T (&arr)[N]) noexcept : data_(arr), size_(N)  // NOLINT(google-explicit-constructor)
		{
		}

		constexpr T*          data() const noexcept { return data_; }
		constexpr std::size_t size() const noexcept { return size_; }
		constexpr bool        empty() const noexcept { return size_ == 0; }

		constexpr T& operator[](std::size_t i) const noexcept { return data_[i]; }

		constexpr T* begin() const noexcept { return data_; }
		constexpr T* end() const noexcept { return data_ + size_; }

	private:
		T*           data_;
		std::size_t  size_;
	};

} // namespace thx
