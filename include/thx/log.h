/*
 *  Created by LuckyNeko on 24/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "thx/thx_api.h"

#include <cstdlib>
#include <memory>
#include <string>

// Detect compiler support for builtin source-location default arguments.
// These let thx::log() capture the caller's file/line/function without a macro.
// GCC/Clang support __builtin_FILE/LINE/FUNCTION natively.
// MSVC supports them since VS 2019 16.6 (_MSC_VER 1926).
#if defined(__GNUC__) || defined(__clang__) || (defined(_MSC_VER) && _MSC_VER >= 1926)
#  define THX_DETAIL_HAS_BUILTIN_LOCATION 1
#endif

namespace thx
{
	enum class LogLevel
	{
		Debug,
		Info,
		Warn,
		Error,
	};

	// Thin source-location descriptor.
	// current() captures the call site via compiler builtins when used as a
	// default argument (C++17-compatible, no macros required). Supported on
	// GCC, Clang, and MSVC >= VS 2019 16.6; empty on older toolchains.
	struct SourceLocation
	{
		const char* file     = "";
		int         line     = 0;
		const char* function = "";

		static constexpr SourceLocation current(
#if defined(THX_DETAIL_HAS_BUILTIN_LOCATION)
			const char* f  = __builtin_FILE(),
			int         ln = __builtin_LINE(),
			const char* fn = __builtin_FUNCTION()
#else
			const char* f  = "",
			int         ln = 0,
			const char* fn = ""
#endif
		) noexcept
		{
			return {f, ln, fn};
		}
	};

	// Diagnostic record passed to ILogSink::write().
	struct LogRecord
	{
		LogLevel       level;
		SourceLocation location;
		std::string    message;
	};

	// Implement this interface and call setLogSink() to intercept all library
	// diagnostics — including ServiceManager and PluginManager messages.
	class THX_API ILogSink
	{
	public:
		// Out-of-line in src/abi.cpp so libthorax owns ILogSink's vtable and
		// typeinfo. See abi.cpp for the cross-DSO rationale.
		virtual ~ILogSink();
		virtual void write(LogRecord const&) = 0;
	};

	// Replaces the process-wide log sink. Passing nullptr SILENCES logging:
	// records are dropped on the floor. Thread-safe; the new sink takes effect
	// for all subsequent log calls. To go back to the built-in stderr sink,
	// call restoreDefaultLogSink().
	THX_API void setLogSink(std::shared_ptr<ILogSink> sink);

	// Restores the built-in stderr log sink. Equivalent to constructing a fresh
	// instance of the default sink and passing it to setLogSink(). Thread-safe.
	THX_API void restoreDefaultLogSink();

	// Emits a log record to the active sink.
	// The source location is captured automatically at the call site on supported
	// compilers (GCC, Clang, MSVC >= VS 2019 16.6).
	THX_API void log(LogLevel            level,
	                 std::string const&  message,
	                 SourceLocation      location = SourceLocation::current());

	// Logs message at Error level if condition is false.
	// Debug builds also call std::abort(); Release builds only log.
	// The condition is always evaluated — never silently swallowed.
	inline void assertThat(bool               condition,
	                         std::string const& message,
	                         SourceLocation     location = SourceLocation::current())
	{
		if (condition)
			return;
		log(LogLevel::Error, message, location);
#if !defined(NDEBUG)
		std::abort();
#endif
	}

} // namespace thx
