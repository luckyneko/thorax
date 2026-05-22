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
		constexpr Span() noexcept : m_data(nullptr), m_size(0) {}

		constexpr Span(T* data, std::size_t size) noexcept : m_data(data), m_size(size) {}

		template <std::size_t N>
		constexpr Span(T (&arr)[N]) noexcept : m_data(arr), m_size(N)  // NOLINT(google-explicit-constructor)
		{
		}

		constexpr T*          data() const noexcept { return m_data; }
		constexpr std::size_t size() const noexcept { return m_size; }
		constexpr bool        empty() const noexcept { return m_size == 0; }

		constexpr T& operator[](std::size_t i) const noexcept { return m_data[i]; }

		constexpr T* begin() const noexcept { return m_data; }
		constexpr T* end() const noexcept { return m_data + m_size; }

	private:
		T*           m_data;
		std::size_t  m_size;
	};

} // namespace thx
