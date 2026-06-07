/*
 *  Created by LuckyNeko on 24/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "thx/rtti/source_location.h"
#include "thx/string_view.h"
#include "thx/thx_api.h"

#include <cstdlib>
#include <string>

// The framework's small, synchronous logging facade.
//
// Core logs its own state changes (ServiceManager / PluginManager diagnostics)
// and needs nothing more than a place to send them. Diagnostics flow through a
// single process-wide *sink* — a C-style function pointer installed via
// setSink(); when none is installed the built-in fallback writes structured
// lines to stderr. There is no formatting layer, no levels-as-filters, no
// fan-out: a backend that wants any of that installs itself as the sink and
// does the work on its own side of the DSO boundary. The richer service-based
// logging (thx::ILogService) is NOT core — it lives in plugins/log (see
// CLAUDE.md "Errors & logging"); the in-tree plugin_log_service bridges this
// sink to a registered ILogService, and plugin_log_spdlog implements one.
//
// The sink is a plain function pointer plus a void* userdata rather than a
// std::function or a registered service: it crosses the libthorax DSO boundary,
// so it must have a stable layout and allocate nothing. Only ABI-stable types
// appear in LogRecord (LogLevel, SourceLocation's C strings, StringView), so a
// plugin compiled against a different STL can implement it safely.
namespace thx
{
	enum class LogLevel
	{
		Debug,
		Info,
		Warn,
		Error,
	};

	// One diagnostic record handed to the sink.
	//
	// LIFETIME: `message` is a non-owning view valid only for the duration of the
	// sink call. A sink that retains the message past the call (e.g. to buffer or
	// forward asynchronously) MUST copy it into its own storage.
	struct LogRecord
	{
		LogLevel level;
		rtti::SourceLocation location;
		StringView message;
	};

	// The process-wide log sink. Receives one record per emit; `userdata` is the
	// pointer handed to setSink().
	//
	// THREAD SAFETY: write() may be called concurrently from any thread —
	// including from inside ServiceManager / PluginManager. The framework
	// serialises the sink slot, so a sink is never invoked concurrently with
	// setSink(), but a sink MAY be invoked concurrently from multiple threads and
	// so must be internally thread-safe. A sink MUST NOT call back into the
	// logging facade (logMessage() or setSink()) — the slot is held for the
	// duration of the call and re-entry deadlocks.
	using LogSink = void (*)(LogRecord const& record, void* userdata);

	// Install the process-wide log sink. Passing a null sink reverts to the
	// built-in stderr writer. Installing a new sink replaces any previous one
	// wholesale (single-owner; there is no stack and no fan-out).
	//
	// DSO-LIFETIME: a sink whose code lives in a plugin DSO MUST be cleared with
	// setSink(nullptr, nullptr) before that DSO is unloaded — the same rule as
	// holding any reference into a plugin across unload. The in-tree
	// plugin_log_service does this in onUnload().
	THX_API void setSink(LogSink sink, void* userdata) noexcept;

	// Emit a diagnostic. The source location is captured automatically at the
	// call site on supported compilers (GCC, Clang, MSVC >= VS 2019 16.6).
	// Forwards to the installed sink, else falls back to stderr.
	//
	// Named logMessage (not log) so the name `thx::log` stays free for the
	// consumer-tier log subsystem's namespace (plugins/log — thx::log::write /
	// ILogService). Core's logging is flat in thx:: by design; the subsystem owns
	// thx::log.
	THX_API void logMessage(LogLevel level,
							std::string const& message,
							rtti::SourceLocation location = rtti::SourceLocation::current());

} // namespace thx
