/*
 *  Created by LuckyNeko on 24/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include <cstdlib>
#include <memory>
#include <string>

// Detect compiler support for builtin source-location default arguments.
// These let thx::log() capture the caller's file/line/function without a macro.
// __builtin_FILE/LINE/FUNCTION are GCC/Clang builtins; MSVC does not expose them
// and its __has_builtin operator emits C4067 when querying unknown builtins.
#if defined(__GNUC__) || defined(__clang__)
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
	// On GCC/Clang, current() captures the call site via compiler builtins when
	// used as a default argument (C++17-compatible, no macros required).
	// On other compilers the macros THX_LOG / THX_ASSERT provide location capture.
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

	// Implement this interface and call set_log_sink() to intercept all library
	// diagnostics — including ServiceManager and PluginLoader messages.
	class ILogSink
	{
	public:
		virtual ~ILogSink() = default;
		virtual void write(LogRecord const&) = 0;
	};

	// Replaces the process-wide log sink. Pass nullptr to restore the default
	// stderr sink. Thread-safe; the new sink is used for all subsequent log calls.
	void set_log_sink(std::shared_ptr<ILogSink> sink);

	// Emits a log record to the active sink.
	// On GCC/Clang the source location is captured automatically at the call site;
	// use THX_LOG for portable location capture.
	void log(LogLevel            level,
	         std::string const&  message,
	         SourceLocation      location = SourceLocation::current());

	// Logs message at Error level if condition is false.
	// Debug builds also call std::abort(); Release builds only log.
	// The condition is always evaluated — never silently swallowed.
	inline void assert_that(bool               condition,
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

// ---------------------------------------------------------------------------
// Portable call-site capture macros
// ---------------------------------------------------------------------------

// THX_LOG(level, msg) — emits a log record with the call site's file/line/func.
// Prefer this form for portable source-location capture on all compilers.
#define THX_LOG(level, msg) \
	thx::log((level), (msg), \
	         thx::SourceLocation{__FILE__, static_cast<int>(__LINE__), __func__})

// THX_ASSERT(cond, msg) — logs + aborts in debug builds if cond is false.
#define THX_ASSERT(cond, msg) \
	thx::assert_that((cond), (msg), \
	                 thx::SourceLocation{__FILE__, static_cast<int>(__LINE__), __func__})
