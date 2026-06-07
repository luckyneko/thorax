/*
 *  Created by LuckyNeko on 24/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "thx/log.h"

#include <cstdio>
#include <mutex>

namespace thx
{
	namespace
	{
		// The process-wide sink slot. A plain function pointer + userdata (no
		// std::function, no service registry) so nothing with an implementation-
		// defined layout crosses the DSO boundary. std::mutex is constant-
		// initialised, so there is no init-order race on first use.
		std::mutex g_sinkMutex;
		LogSink g_sink = nullptr;
		void* g_userdata = nullptr;

		// Fallback when no sink is installed: structured line to stderr.
		// Format: [thorax][LEVEL] file:line function: message
		void writeStderr(LogRecord const& r)
		{
			static const char* const kLevel[] = {"DEBUG", "INFO", "WARN", "ERROR"};
			fprintf(stderr, "[thorax][%s] %s:%d %s: %.*s\n",
					kLevel[static_cast<int>(r.level)],
					r.location.file,
					r.location.line,
					r.location.function,
					static_cast<int>(r.message.size()),
					r.message.data());
		}
	} // namespace

	void setSink(LogSink sink, void* userdata) noexcept
	{
		// Held against write(): a sink is never invoked concurrently with a swap,
		// so a plugin clearing the sink in onUnload() is guaranteed no in-flight
		// call into its (about-to-be-unmapped) code once setSink returns.
		std::lock_guard<std::mutex> lock(g_sinkMutex);
		g_sink = sink;
		g_userdata = userdata;
	}

	void logMessage(LogLevel level, std::string const& message, rtti::SourceLocation location)
	{
		LogRecord record{level, location, StringView{message.c_str(), message.size()}};

		// The lock is held across the sink call so setSink() can synchronise
		// against in-flight emits (see above). The sink MUST NOT re-enter the
		// logging facade — the mutex is not recursive (documented on LogSink).
		std::lock_guard<std::mutex> lock(g_sinkMutex);
		if (g_sink)
			g_sink(record, g_userdata);
		else
			writeStderr(record);
	}

} // namespace thx
