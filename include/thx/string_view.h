/*
 *  Created by LuckyNeko on 24/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace thx
{
	// ABI-stable non-owning string reference.
	//
	// std::string_view is not ABI-stable across DSO boundaries because its
	// binary layout is implementation-defined. thx::StringView has a guaranteed
	// layout of { const char* data, size_t size } and is safe to pass through
	// virtual method signatures that cross plugin boundaries.
	//
	// The conversion to std::string_view is intentionally explicit so that
	// ABI-sensitive interfaces cannot accidentally regress to the stdlib type.
	class StringView
	{
	public:
		constexpr StringView() noexcept
			: m_data("")
			, m_size(0)
		{
		}

		constexpr StringView(const char* str, std::size_t len) noexcept
			: m_data(str ? str : "")
			, m_size(len)
		{
		}

		// Implicit construction from string literals so existing call sites
		// (e.g. Version{1, 0, 0, "alpha"}) do not need updating.
		constexpr StringView(const char* str) noexcept // NOLINT(google-explicit-constructor)
			: m_data(str ? str : "")
			, m_size(str ? std::char_traits<char>::length(str) : 0)
		{
		}

		constexpr const char* data() const noexcept { return m_data; }
		constexpr std::size_t size() const noexcept { return m_size; }
		constexpr bool empty() const noexcept { return m_size == 0; }

		constexpr const char* begin() const noexcept { return m_data; }
		constexpr const char* end() const noexcept { return m_data + m_size; }

		constexpr char operator[](std::size_t i) const noexcept { return m_data[i]; }

		// Explicit conversion for code that needs the stdlib type internally.
		explicit constexpr operator std::string_view() const noexcept
		{
			return {m_data, m_size};
		}

		constexpr bool operator==(StringView const& other) const noexcept
		{
			if (m_size != other.m_size)
				return false;
			for (std::size_t i = 0; i < m_size; ++i)
				if (m_data[i] != other.m_data[i])
					return false;
			return true;
		}

		constexpr bool operator!=(StringView const& other) const noexcept
		{
			return !(*this == other);
		}

		constexpr bool operator<(StringView const& other) const noexcept
		{
			std::size_t n = m_size < other.m_size ? m_size : other.m_size;
			for (std::size_t i = 0; i < n; ++i)
			{
				if (m_data[i] < other.m_data[i])
					return true;
				if (m_data[i] > other.m_data[i])
					return false;
			}
			return m_size < other.m_size;
		}

		constexpr bool operator<=(StringView const& other) const noexcept
		{
			return !(other < *this);
		}
		constexpr bool operator>(StringView const& other) const noexcept
		{
			return other < *this;
		}
		constexpr bool operator>=(StringView const& other) const noexcept
		{
			return !(*this < other);
		}

	private:
		const char* m_data;
		std::size_t m_size;
	};

	// ABI lock-down — StringView crosses plugin boundaries; pin the layout
	// we promised the docs (single ptr + size_t, no padding under standard ABIs).
	static_assert(sizeof(StringView) == sizeof(const char*) + sizeof(std::size_t),
				  "StringView must have layout { const char* data, size_t size } for the documented ABI");

} // namespace thx
