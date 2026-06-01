/*
 *  Created by LuckyNeko on 24/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "thx/log/log.h"

#include "thx/log/log_service.h"
#include "thx/service/service.h"

#include <cstdio>

namespace thx::log
{

	namespace
	{
		// Fallback when no ILogService is registered: structured line to stderr.
		// Format: [thorax][LEVEL] file:line function: message
		void writeStderr(LogLevel level, SourceLocation const& loc, std::string const& message)
		{
			static const char* const kLevel[] = {"DEBUG", "INFO", "WARN", "ERROR"};
			fprintf(stderr, "[thorax][%s] %s:%d %s: %s\n",
					kLevel[static_cast<int>(level)],
					loc.file,
					loc.line,
					loc.function,
					message.c_str());
		}
	} // namespace

	void write(LogLevel level, std::string const& message, SourceLocation location)
	{
		// Forward to the registered logging service if one exists. getService
		// returns an empty handle (no diagnostic) when none is registered, so
		// this is recursion-safe even before any ILogService is installed. The
		// handle is dropped at the end of this call — it is never held across
		// calls, so a plugin-provided service can be unregistered and its DSO
		// unloaded without leaving a dangling reference.
		if (auto svc = thx::service::getService<ILogService>())
		{
			svc->write(LogRecord{level, location, StringView{message.c_str(), message.size()}});
			return;
		}
		writeStderr(level, location, message);
	}

} // namespace thx::log
