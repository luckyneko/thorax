/*
 *  Created by LuckyNeko on 22/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include <cstdint>
#include <string_view>

namespace thx
{
	namespace detail
	{
		constexpr bool sv_all_digits(std::string_view s) noexcept
		{
			if (s.empty())
				return false;
			for (char c : s)
				if (c < '0' || c > '9')
					return false;
			return true;
		}

		constexpr uint64_t sv_parse_uint(std::string_view s) noexcept
		{
			uint64_t v = 0;
			for (char c : s)
				v = v * 10 + static_cast<uint64_t>(c - '0');
			return v;
		}

		// Removes and returns the first dot-delimited token from `s`.
		constexpr std::string_view sv_pop_id(std::string_view& s) noexcept
		{
			if (s.empty())
				return {};
			auto dot = s.find('.');
			if (dot == std::string_view::npos)
			{
				auto id = s;
				s = {};
				return id;
			}
			auto id = s.substr(0, dot);
			s = s.substr(dot + 1);
			return id;
		}

		// Compares semver pre-release strings according to semver 2.0 §11.
		// Returns -1, 0, or 1. build_metadata is intentionally excluded.
		//
		// Key rules:
		//   - A version with pre-release has lower precedence than the release:
		//       compare_pre_release("alpha", "") == -1
		//   - Identifiers are compared left to right, dot-separated.
		//   - Purely numeric identifiers are compared as integers.
		//   - Alphanumeric identifiers are compared lexicographically (ASCII).
		//   - Numeric identifiers always have lower precedence than alphanumeric.
		//   - Fewer identifiers = lower precedence when all prior are equal.
		constexpr int compare_pre_release(std::string_view a, std::string_view b) noexcept
		{
			if (a.empty() && b.empty())
				return 0;
			if (a.empty())
				return 1; // release > pre-release
			if (b.empty())
				return -1; // pre-release < release

			while (true)
			{
				if (a.empty() && b.empty())
					return 0;
				if (a.empty())
					return -1; // fewer identifiers = lower precedence
				if (b.empty())
					return 1;

				auto ia = sv_pop_id(a);
				auto ib = sv_pop_id(b);

				bool a_num = sv_all_digits(ia);
				bool b_num = sv_all_digits(ib);

				if (a_num && b_num)
				{
					auto va = sv_parse_uint(ia);
					auto vb = sv_parse_uint(ib);
					if (va != vb)
						return va < vb ? -1 : 1;
				}
				else if (a_num)
				{
					return -1; // numeric < alphanumeric per semver 2.0 §11.4.1
				}
				else if (b_num)
				{
					return 1;
				}
				else
				{
					if (ia < ib)
						return -1;
					if (ia > ib)
						return 1;
				}
			}
		}

	} // namespace detail
} // namespace thx
