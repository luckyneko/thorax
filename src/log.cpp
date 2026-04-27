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
#include <shared_mutex>

namespace thx
{

namespace
{

// Default sink: writes structured lines to stderr.
// Format: [thorax][LEVEL] file:line function: message
class StderrSink : public ILogSink
{
public:
	void write(LogRecord const& r) override
	{
		static const char* const kLevel[] = {"DEBUG", "INFO", "WARN", "ERROR"};
		int lvl = static_cast<int>(r.level);
		fprintf(stderr, "[thorax][%s] %s:%d %s: %s\n",
		        kLevel[lvl],
		        r.location.file,
		        r.location.line,
		        r.location.function,
		        r.message.c_str());
	}
};

std::shared_mutex         g_sink_mutex;
std::shared_ptr<ILogSink> g_sink = std::make_shared<StderrSink>();

} // namespace

void set_log_sink(std::shared_ptr<ILogSink> sink)
{
	std::unique_lock lock(g_sink_mutex);
	g_sink = std::move(sink); // nullptr → silence
}

void restore_default_log_sink()
{
	std::unique_lock lock(g_sink_mutex);
	g_sink = std::make_shared<StderrSink>();
}

void log(LogLevel level, std::string const& message, SourceLocation location)
{
	std::shared_ptr<ILogSink> sink;
	{
		std::shared_lock lock(g_sink_mutex);
		sink = g_sink;
	}
	if (sink)
		sink->write({level, location, message});
}

} // namespace thx
