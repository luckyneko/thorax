/*
 *  Created by LuckyNeko on 24/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

// Detect compiler support for builtin source-location default arguments.
// These let thx::log::write() capture the caller's file/line/function without a
// macro. GCC/Clang support __builtin_FILE/LINE/FUNCTION natively. MSVC supports
// them since VS 2019 16.6 (_MSC_VER 1926).
#if defined(__GNUC__) || defined(__clang__) || (defined(_MSC_VER) && _MSC_VER >= 1926)
#	define THX_DETAIL_HAS_BUILTIN_LOCATION 1
#endif

namespace thx::log
{
	// Thin source-location descriptor.
	// current() captures the call site via compiler builtins when used as a
	// default argument (C++17-compatible, no macros required). Supported on
	// GCC, Clang, and MSVC >= VS 2019 16.6; empty on older toolchains.
	struct SourceLocation
	{
		const char* file = "";
		int line = 0;
		const char* function = "";

		static constexpr SourceLocation current(
#if defined(THX_DETAIL_HAS_BUILTIN_LOCATION)
			const char* f = __builtin_FILE(),
			int ln = __builtin_LINE(),
			const char* fn = __builtin_FUNCTION()
#else
			const char* f = "",
			int ln = 0,
			const char* fn = ""
#endif
				) noexcept
		{
			return {f, ln, fn};
		}
	};

} // namespace thx::log
