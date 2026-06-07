/*
 *  Created by LuckyNeko on 07/06/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "thx/log/log_record.h"
#include "thx/log/log_service.h"
#include "thx/rtti/source_location.h"
#include "thx/service/service.h"
#include "thx/string_view.h"

#include <cstdio>

// The log subsystem's emit facade. Emit a diagnostic at a level (with the call
// site captured automatically) and it is forwarded to the registered
// ILogService.
//
// These are header-only inline shims over the registered ILogService — the log
// subsystem is not part of libthorax, so there is no exported symbol here. Load
// a backend plugin (plugins/log/spdlog) — directly, or via the plugin_log_service
// bridge that also routes core's own diagnostics — to install one. With no
// ILogService registered, every call falls back to stderr, matching core's
// fallback so diagnostics are never lost. The ILogService handle is held only
// for the duration of each call (never across calls), so the backend plugin can
// be unloaded with no dangling reference.
//
// This mirrors the io facade (thx::io::open / addHandler in <thx/io/io.h>):
// core never logs through here — core uses its own thx::logMessage() in <thx/log.h>;
// this is the consumer-tier surface that resolves the service directly.
namespace thx::log
{
	// Forward `record` to the registered ILogService, else write it to stderr.
	inline void write(LogRecord const& record)
	{
		if (auto svc = thx::service::getService<ILogService>())
		{
			svc->write(record);
			return;
		}

		static const char* const kLevel[] = {"DEBUG", "INFO", "WARN", "ERROR"};
		std::fprintf(stderr, "[thorax][%s] %s:%d %s: %.*s\n",
					 kLevel[static_cast<int>(record.level)],
					 record.location.file, record.location.line, record.location.function,
					 static_cast<int>(record.message.size()), record.message.data());
	}

	// Emit a diagnostic at `level`. The source location is captured automatically
	// at the call site on supported compilers (GCC, Clang, MSVC >= VS 2019 16.6).
	inline void write(LogLevel level, StringView message,
					  thx::rtti::SourceLocation location = thx::rtti::SourceLocation::current())
	{
		write(LogRecord{level, location, message});
	}

	// Level shortcuts.
	inline void debug(StringView message,
					  thx::rtti::SourceLocation location = thx::rtti::SourceLocation::current())
	{
		write(LogRecord{LogLevel::Debug, location, message});
	}

	inline void info(StringView message,
					 thx::rtti::SourceLocation location = thx::rtti::SourceLocation::current())
	{
		write(LogRecord{LogLevel::Info, location, message});
	}

	inline void warn(StringView message,
					 thx::rtti::SourceLocation location = thx::rtti::SourceLocation::current())
	{
		write(LogRecord{LogLevel::Warn, location, message});
	}

	inline void error(StringView message,
					  thx::rtti::SourceLocation location = thx::rtti::SourceLocation::current())
	{
		write(LogRecord{LogLevel::Error, location, message});
	}

} // namespace thx::log
