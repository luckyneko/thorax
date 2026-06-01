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
#include "thx/string_view.h"

namespace thx::log
{
	// Diagnostic record passed to ILogService::write().
	//
	// LIFETIME: `message` is a non-owning view valid only for the duration of
	// the write() call. A service that retains the message past write() (e.g.
	// to buffer or forward asynchronously) MUST copy it into its own storage.
	struct LogRecord
	{
		LogLevel level;
		SourceLocation location;
		StringView message;
	};

} // namespace thx::log
