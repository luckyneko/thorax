/*
 *  Created by LuckyNeko on 25/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include <thx/service/service.h>

#include <memory>

namespace thx::plugins::logging
{

enum class LogLevel : int { Debug = 0, Info = 1, Warn = 2, Error = 3 };

// Provider interface.  Implement this to contribute a custom log destination.
// The LoggingService holds only a std::weak_ptr<ILogBackend>; when the caller
// drops its shared_ptr the backend is automatically evicted on the next log().
class ILogBackend
{
public:
	virtual ~ILogBackend() = default;
	virtual void write(LogLevel level, const char* message) = 0;
};

// Main logging service.  Include this header in any plugin or host that wants
// to emit log messages or contribute a new backend.
//
// Usage:
//   auto log = sm.getService<ILoggingService>();
//   auto con = log->makeConsoleBackend();
//   log->addBackend(con);
//   log->log(LogLevel::Info, "hello");
//   log->removeBackend(con.get());   // or just let con go out of scope
class ILoggingService : public thx::service::Service<ILoggingService>
{
public:
	static constexpr thx::Version staticVersion()
	{
		return thx::Version{1, 0, 0};
	}

	// Route a message to all live backends.
	virtual void log(LogLevel level, const char* message) = 0;

	// Register an external backend.  The service stores a weak_ptr; when the
	// caller drops its shared_ptr the backend is evicted automatically.
	virtual void addBackend(std::shared_ptr<ILogBackend> backend) = 0;

	// Eagerly remove a backend by raw-pointer identity before it expires.
	virtual void removeBackend(ILogBackend* backend) = 0;

	// Built-in backend factories.  All allocations happen inside the plugin so
	// the paired deleter always frees from the correct heap.
	virtual std::shared_ptr<ILogBackend> makeConsoleBackend() = 0;
	virtual std::shared_ptr<ILogBackend> makeFileBackend(const char* path) = 0;
	virtual std::shared_ptr<ILogBackend> makeRotatingFileBackend(
		const char* path, int max_size_bytes, int max_files) = 0;
};

} // namespace thx::plugins::logging
