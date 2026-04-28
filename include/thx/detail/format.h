/*
 *  Created by LuckyNeko on 28/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "thx/version_type.h"

#include <string>

namespace thx
{
	namespace detail
	{
		// Returns "major.minor.patch" as a std::string.
		// Pre-release and build metadata are intentionally omitted; this is used
		// only for diagnostic messages where the three numeric components suffice.
		inline std::string format_version(Version const& v)
		{
			return std::to_string(v.major) + '.'
			     + std::to_string(v.minor) + '.'
			     + std::to_string(v.patch);
		}
	} // namespace detail
} // namespace thx
