/*
 *  Created by LuckyNeko on 24/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "thx/log/log_record.h"
#include "thx/service/iservice.h"
#include "thx/version_type.h"

namespace thx::log
{
	// The framework's logging service interface. It is an ordinary service: a
	// single implementation is registered with the ServiceManager under the id
	// "thx.log.ILogService", and thx::log::write() forwards every diagnostic to
	// it (falling back to stderr when none is registered). Register one with
	//   thx::service::registerService<MyLogService>();
	// or ship it from a plugin via THX_DEFINE_SERVICE_PLUGIN(MyLogService).
	//
	// Single-owner, like every service: exactly one ILogService may be
	// registered process-wide. Install it from the host OR a plugin, not both.
	//
	// Being a plain service interface, it needs no abi.cpp anchor — getService
	// resolves it by id and static_cast, never dynamic_cast, so its typeinfo
	// does not have to coalesce across DSOs (see the io/logging service shapes).
	class ILogService : public thx::service::Service<ILogService>
	{
	public:
		static constexpr thx::Version staticVersion()
		{
			return thx::Version{1, 0, 0};
		}

		// Consume one diagnostic record. See LogRecord for the message-lifetime
		// contract. Only ABI-stable types cross this boundary (LogLevel,
		// SourceLocation's C strings, StringView), so a plugin may implement it.
		//
		// THREAD SAFETY: implementations MUST be thread-safe. thx::log::write()
		// resolves the service per call and may be invoked concurrently from any
		// thread — including from inside ServiceManager / PluginManager while they
		// hold their own locks — so write() must not assume serialised calls and
		// must not call back into the logging facade. (The in-tree spdlog service
		// uses spdlog's `_mt` sinks, which are internally synchronised.)
		virtual void write(LogRecord const& record) = 0;
	};

} // namespace thx::log
