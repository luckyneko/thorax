/*
 *  Created by LuckyNeko on 07/06/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "thx/rtti/source_location.h"
#include "thx/string_view.h"

// The log subsystem's own diagnostic value types.
//
// The log subsystem is a separate project built on thorax's plugin/service
// machinery — it is NOT part of libthorax core and does NOT depend on core's
// logging facade (<thx/log.h>). So it carries its own LogLevel / LogRecord in
// the thx::log namespace rather than drawing on core's thx::LogLevel /
// thx::LogRecord, exactly as the io subsystem carries its own error domain
// (thx::io::Error) instead of core's thx::ErrorCode.
//
// Only ABI-stable primitives appear here — the LogLevel enum, the
// SourceLocation C strings, and StringView — so the types cross the DSO boundary
// safely and a backend compiled against a different STL can implement
// ILogService::write(). (StringView and SourceLocation are core ABI primitives
// shared by every plugin interface, the same way io reuses StringView.)
namespace thx::log
{
	enum class LogLevel
	{
		Debug,
		Info,
		Warn,
		Error,
	};

	// One diagnostic record handed to an ILogService.
	//
	// LIFETIME: `message` is a non-owning view valid only for the duration of the
	// write() call. A service that retains the message past the call (e.g. to
	// buffer or forward asynchronously) MUST copy it into its own storage.
	struct LogRecord
	{
		LogLevel level;
		thx::rtti::SourceLocation location;
		thx::StringView message;
	};

} // namespace thx::log
