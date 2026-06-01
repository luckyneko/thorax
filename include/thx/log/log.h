/*
 *  Created by LuckyNeko on 24/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "thx/log/log_level.h"
#include "thx/log/source_location.h"
#include "thx/thx_api.h"

#include <cstdlib>
#include <string>

// The framework's logging *emit* facade. Diagnostics flow through the
// registered thx::log::ILogService (see thx/log/log_service.h); when none is
// registered the built-in fallback writes structured lines to stderr. There is
// no global sink slot and no setter — the service registry is the single source
// of truth.
//
// This header deliberately does NOT include log_service.h: it is the low-level
// "I want to emit a log" surface, included transitively by version_type.h /
// result.h (which call assertThat). Code that *implements or registers* the
// service includes thx/log/log_service.h instead. (Mirrors iservice.h vs
// service.h.)
namespace thx::log
{
	// Emit a diagnostic. The source location is captured automatically at the
	// call site on supported compilers (GCC, Clang, MSVC >= VS 2019 16.6).
	// Forwards to the registered ILogService, else falls back to stderr.
	THX_API void write(LogLevel level,
					   std::string const& message,
					   SourceLocation location = SourceLocation::current());

	// Convenience shortcuts for each level. Inline so the SourceLocation default
	// is captured at the caller's site, then forwarded to write().
	inline void debug(std::string const& message,
					  SourceLocation location = SourceLocation::current())
	{
		write(LogLevel::Debug, message, location);
	}
	inline void info(std::string const& message,
					 SourceLocation location = SourceLocation::current())
	{
		write(LogLevel::Info, message, location);
	}
	inline void warn(std::string const& message,
					 SourceLocation location = SourceLocation::current())
	{
		write(LogLevel::Warn, message, location);
	}
	inline void error(std::string const& message,
					  SourceLocation location = SourceLocation::current())
	{
		write(LogLevel::Error, message, location);
	}

	// Logs message at Error level if condition is false.
	// Debug builds also call std::abort(); Release builds only log.
	// The condition is always evaluated — never silently swallowed.
	inline void assertThat(bool condition,
						   std::string const& message,
						   SourceLocation location = SourceLocation::current())
	{
		if (condition)
			return;
		write(LogLevel::Error, message, location);
#if !defined(NDEBUG)
		std::abort();
#endif
	}

} // namespace thx::log
