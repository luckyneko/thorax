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
		constexpr StringView() noexcept : data_(""), size_(0) {}

		constexpr StringView(const char* str, std::size_t len) noexcept
			: data_(str ? str : ""), size_(len)
		{
		}

		// Implicit construction from string literals so existing call sites
		// (e.g. Version{1, 0, 0, "alpha"}) do not need updating.
		constexpr StringView(const char* str) noexcept  // NOLINT(google-explicit-constructor)
			: data_(str ? str : "")
			, size_(str ? std::char_traits<char>::length(str) : 0)
		{
		}

		constexpr const char* data() const noexcept { return data_; }
		constexpr std::size_t size() const noexcept { return size_; }
		constexpr bool        empty() const noexcept { return size_ == 0; }

		constexpr const char* begin() const noexcept { return data_; }
		constexpr const char* end() const noexcept { return data_ + size_; }

		constexpr char operator[](std::size_t i) const noexcept { return data_[i]; }

		// Explicit conversion for code that needs the stdlib type internally.
		explicit constexpr operator std::string_view() const noexcept
		{
			return {data_, size_};
		}

		constexpr bool operator==(StringView const& other) const noexcept
		{
			if (size_ != other.size_)
				return false;
			for (std::size_t i = 0; i < size_; ++i)
				if (data_[i] != other.data_[i])
					return false;
			return true;
		}

		constexpr bool operator!=(StringView const& other) const noexcept
		{
			return !(*this == other);
		}

		constexpr bool operator<(StringView const& other) const noexcept
		{
			std::size_t n = size_ < other.size_ ? size_ : other.size_;
			for (std::size_t i = 0; i < n; ++i)
			{
				if (data_[i] < other.data_[i])
					return true;
				if (data_[i] > other.data_[i])
					return false;
			}
			return size_ < other.size_;
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
		const char*  data_;
		std::size_t  size_;
	};

} // namespace thx
