/*
 *  Created by LuckyNeko on 22/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace thx
{
	namespace detail
	{
		// FNV-1a 64-bit hash of a string.
		constexpr uint64_t fnv1a_hash(std::string_view str) noexcept
		{
			uint64_t hash = 14695981039346656037ULL;
			for (unsigned char c : str)
			{
				hash ^= static_cast<uint64_t>(c);
				hash *= 1099511628211ULL;
			}
			return hash;
		}
	} // namespace detail
} // namespace thx
