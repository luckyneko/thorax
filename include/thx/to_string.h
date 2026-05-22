/*
 *  Created by LuckyNeko on 21/05/2026.
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
	// Non-template overloads in the thx:: namespace, mirroring std::to_string.
	// Centralised here so the core type headers don't have to pull <string>.
	// Add new overloads as new printable types are introduced.

	inline std::string toString(Version const& v)
	{
		return std::to_string(v.major) + '.'
		     + std::to_string(v.minor) + '.'
		     + std::to_string(v.patch);
	}

} // namespace thx
