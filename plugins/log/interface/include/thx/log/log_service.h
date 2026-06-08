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
	// The log subsystem's service interface. It is NOT part of libthorax core
	// (which logs through the plain thx::LogSink — see <thx/log.h>); it is a
	// consumer-tier interface owned by the log_interface library under
	// plugins/log, mirroring io's IIoService. A backend implements it and
	// registers it with the ServiceManager under the id "thx.log.ILogService";
	// the in-tree plugin_log_service then installs a core LogSink that forwards
	// every core diagnostic to the registered ILogService, so loading a logger
	// routes the whole process's logging through it. Register one with
	//   thx::service::registerService<MyLogService>();
	// or ship it from a plugin via THX_DEFINE_SERVICE_PLUGIN(MyLogService).
	//
	// Single-owner, like every service: exactly one ILogService may be
	// registered process-wide. Install it from the host OR a plugin, not both.
	//
	// Being a plain service interface, it needs no abi.cpp anchor — getService
	// resolves it by id and static_cast, never dynamic_cast, so its typeinfo
	// does not have to coalesce across DSOs (see the io service shape).
	class ILogService : public thx::service::Service<ILogService>
	{
	public:
		static constexpr thx::Version staticVersion()
		{
			return thx::Version{1, 0, 0};
		}

		// Consume one diagnostic record. See LogRecord (in <thx/log/log_record.h>)
		// for the message-lifetime contract. Only ABI-stable types cross this
		// boundary (LogLevel, SourceLocation's C strings, StringView), so a plugin
		// may implement it.
		//
		// THREAD SAFETY: implementations MUST be thread-safe. The forwarding sink
		// resolves the service per call and may be invoked concurrently from any
		// thread — including from inside ServiceManager / PluginManager — so
		// write() must not assume serialised calls and must not call back into the
		// logging facade. (The in-tree spdlog service uses spdlog's `_mt` sinks,
		// which are internally synchronised.)
		virtual void write(const LogRecord& record) = 0;
	};

} // namespace thx::log
