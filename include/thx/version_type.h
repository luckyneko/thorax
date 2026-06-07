/*
 *  Created by LuckyNeko on 22/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include <cstdint>

namespace thx
{
	// A three-component numeric version (major.minor.patch).
	// Trivial layout — safe to pass through virtual signatures across DSO
	// boundaries.
	//
	// pack() / Version(uint32_t) convert to and from a fixed wire encoding used
	// to cross the C plugin ABI (where struct returns are unsafe). The encoding
	// is the bridge for thx_abi_version(); Version's in-memory layout is
	// deliberately separate from it.
	//
	//   bits 24..31 → major  (0..255)
	//   bits 16..23 → minor  (0..255)
	//   bits  0..15 → patch  (0..65535)
	//
	// Components above their bit range trip an assertion in pack(); aborts in
	// Debug, logs at Error in Release. Silent truncation would let an
	// incompatible ABI version round-trip to a compatible-looking one.
	struct Version
	{
		uint32_t major{0};
		uint32_t minor{0};
		uint32_t patch{0};

		constexpr Version() noexcept = default;

		constexpr Version(uint32_t maj, uint32_t min, uint32_t pat) noexcept
			: major(maj)
			, minor(min)
			, patch(pat)
		{
		}

		// Unpack from the wire encoding (see comment on the struct).
		constexpr explicit Version(uint32_t packed) noexcept
			: major((packed >> 24) & 0xFFu)
			, minor((packed >> 16) & 0xFFu)
			, patch(packed & 0xFFFFu)
		{
		}

		// Pack to the wire encoding (see comment on the struct). Not constexpr
		// because the overflow check goes through the logging facade; pack()
		// is exclusively used at runtime (thx_abi_version() exports), so the
		// constexpr was theoretical anyway.
		uint32_t pack() const noexcept
		{
			return ((major & 0xFFu) << 24) | ((minor & 0xFFu) << 16) | (patch & 0xFFFFu);
		}

		constexpr bool operator==(Version const& o) const noexcept
		{
			return major == o.major && minor == o.minor && patch == o.patch;
		}
		constexpr bool operator!=(Version const& o) const noexcept { return !(*this == o); }
		constexpr bool operator<(Version const& o) const noexcept
		{
			if (major != o.major)
				return major < o.major;
			if (minor != o.minor)
				return minor < o.minor;
			return patch < o.patch;
		}
		constexpr bool operator<=(Version const& o) const noexcept { return !(o < *this); }
		constexpr bool operator>(Version const& o) const noexcept { return o < *this; }
		constexpr bool operator>=(Version const& o) const noexcept { return !(*this < o); }

		// Returns true if `provided` is backwards-compatible with `required`:
		// same major version and provided >= required.
		static constexpr bool compatible(Version const& required, Version const& provided) noexcept
		{
			return provided.major == required.major && provided >= required;
		}
	};

} // namespace thx
