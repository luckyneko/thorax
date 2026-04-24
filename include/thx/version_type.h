/*
 *  Created by LuckyNeko on 22/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "thx/detail/semver.h"
#include "thx/string_view.h"

namespace thx
{
	// Semantic version following semver.org 2.0.
	// build_metadata is stored but ignored for all precedence comparisons.
	//
	// pre_release and build_metadata use thx::StringView (not std::string_view)
	// so that Version has a guaranteed binary layout when passed through virtual
	// methods across DSO boundaries.
	struct Version
	{
		uint32_t   major{0};
		uint32_t   minor{0};
		uint32_t   patch{0};
		StringView pre_release{};    // empty means release version
		StringView build_metadata{}; // ignored for precedence per semver 2.0

		constexpr Version() noexcept = default;

		constexpr Version(uint32_t maj, uint32_t min, uint32_t pat,
						  StringView pre   = {},
						  StringView build = {}) noexcept
			: major(maj)
			, minor(min)
			, patch(pat)
			, pre_release(pre)
			, build_metadata(build)
		{
		}

		// Returns -1, 0, or 1. build_metadata is ignored per semver 2.0.
		constexpr int compare(Version const& other) const noexcept
		{
			if (major != other.major)
				return major < other.major ? -1 : 1;
			if (minor != other.minor)
				return minor < other.minor ? -1 : 1;
			if (patch != other.patch)
				return patch < other.patch ? -1 : 1;
			return detail::compare_pre_release(
				static_cast<std::string_view>(pre_release),
				static_cast<std::string_view>(other.pre_release));
		}

		constexpr bool operator==(Version const& o) const noexcept { return compare(o) == 0; }
		constexpr bool operator!=(Version const& o) const noexcept { return compare(o) != 0; }
		constexpr bool operator<(Version const& o) const noexcept { return compare(o) < 0; }
		constexpr bool operator<=(Version const& o) const noexcept { return compare(o) <= 0; }
		constexpr bool operator>(Version const& o) const noexcept { return compare(o) > 0; }
		constexpr bool operator>=(Version const& o) const noexcept { return compare(o) >= 0; }
	};

	constexpr Version make_version(uint32_t major, uint32_t minor, uint32_t patch,
								   StringView pre_release    = {},
								   StringView build_metadata = {}) noexcept
	{
		return {major, minor, patch, pre_release, build_metadata};
	}

	// Returns true if `provided` is backwards-compatible with `required`:
	// same major version and provided >= required (pre-release aware).
	constexpr bool compatible(Version const& required, Version const& provided) noexcept
	{
		return provided.major == required.major && provided >= required;
	}

} // namespace thx
