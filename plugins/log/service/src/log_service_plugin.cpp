/*
 *  Created by LuckyNeko on 06/06/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

// The log subsystem's "service" plugin: the bridge between libthorax core's
// plain thx::LogSink (<thx/log.h>) and the service-based ILogService world
// (<thx/log/log_service.h>, owned by log_interface).
//
// Core knows nothing about ILogService — it just calls an installed LogSink. On
// load this plugin installs a sink that forwards every core diagnostic to the
// registered ILogService (falling back to stderr when none is registered); on
// unload it clears the sink, restoring core's built-in stderr writer. It
// registers no service of its own.
//
// It `requires` thx.log.ILogService so loadWithDependencies(this plugin) pulls
// in a logger backend (e.g. plugin_log_spdlog) and loads it first — the same
// dependency shape io's file handler has on the IIoService provider. A backend
// implements ILogService; this bridge is backend-agnostic.
//
// The bridge is the one place that touches both logging worlds: it includes
// core's <thx/log.h> (for the LogSink slot and core's LogRecord) AND the
// subsystem's <thx/log/log_service.h> (for thx::log::ILogService and its own
// LogRecord), translating each core record into a thx::log::LogRecord on the
// way through. Everywhere else, the two stay independent.

#include <thx/log.h>
#include <thx/log/log_service.h>
#include <thx/plugin/iplugin.h>
#include <thx/plugin/platform.h>
#include <thx/service/service.h>

#include <cstdio>

namespace
{
	// The LogSink core invokes for every diagnostic. Resolves the registered
	// ILogService per call (holding the handle only for this call, never across
	// calls — so the backend plugin can be unloaded without leaving a dangling
	// reference) and forwards the record. Falls back to stderr when no backend is
	// registered, matching core's own fallback so diagnostics are never lost.
	//
	// NOTE: getService<ILogService>() takes the ServiceManager read lock, so any
	// code holding the ServiceManager write lock must NOT log until it releases
	// it (ServiceManager already defers its own diagnostics for exactly this
	// reason — see service_manager.cpp).
	void forwardToService(const thx::LogRecord& r, void*)
	{
		if (auto svc = thx::service::getService<thx::log::ILogService>())
		{
			// Translate core's record into the log subsystem's own type. The two
			// LogLevel enums share enumerator order, and location/message are
			// shared ABI primitives (SourceLocation, StringView).
			thx::log::LogRecord rec{
				static_cast<thx::log::LogLevel>(r.level), r.location, r.message};
			svc->write(rec);
			return;
		}

		static const char* const kLevel[] = {"DEBUG", "INFO", "WARN", "ERROR"};
		std::fprintf(stderr, "[thorax][%s] %s:%d %s: %.*s\n",
					 kLevel[static_cast<int>(r.level)],
					 r.location.file, r.location.line, r.location.function,
					 static_cast<int>(r.message.size()), r.message.data());
	}

	struct LogServicePlugin : thx::plugin::IPlugin
	{
		thx::StringView name() const override { return "thx.log.LogService"; }
		thx::Version version() const override { return thx::Version{1, 0, 0}; }

		// Require a logger backend so loadWithDependencies pulls one in and loads
		// it before this bridge (the requirement is satisfied by the registered
		// ILogService at load time).
		thx::Span<const thx::plugin::ServiceRequirement> required() const override
		{
			static const thx::plugin::ServiceRequirement kRequires[] = {
				{thx::log::ILogService::staticId(), thx::log::ILogService::staticVersion()},
			};
			return {kRequires, 1};
		}

		bool onLoad() override
		{
			thx::setSink(&forwardToService, nullptr);
			return true;
		}

		void onUnload() override
		{
			// Clear before our DSO is unmapped: the sink fn lives here.
			thx::setSink(nullptr, nullptr);
		}
	};

} // namespace

THX_DEFINE_PLUGIN(LogServicePlugin)
