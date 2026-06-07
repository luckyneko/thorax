/*
 *  Created by LuckyNeko on 25/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

// Demonstrates implementing the log subsystem's service interface with spdlog.
// The plugin registers a single thx::log::ILogService (from log_interface, NOT
// core). It does not touch core's LogSink itself — plugin_log_service installs
// the sink that forwards core diagnostics to whatever ILogService is registered,
// so loading this backend alongside that bridge routes every core diagnostic and
// every thx::log::write() / debug() / info() / warn() / error() call across the
// whole process (host + libthorax internals + other plugins) here.
// (loadWithDependencies(bridge) pulls this backend in.) Unload and logging falls
// back to the built-in stderr writer.
//
// Identity split (see CLAUDE.md "Errors & logging"): the *service* is registered
// under the stable interface id "thx.log.ILogService"; the *logger's own*
// name/version is the plugin's manifest identity ("thx.spdlog.SpdlogService"
// below). A host chooses a logger by discovering the plugins that provide
// ILogService — pluginsProviding<ILogService>() lists them by their distinct
// plugin names. Single-owner: one logger is active at a time.

#include <thx/log/log_service.h>
#include <thx/plugin/iplugin.h>
#include <thx/plugin/platform.h>
#include <thx/service/service.h>

#include <spdlog/logger.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <string_view>

namespace
{

	spdlog::level::level_enum toSpd(thx::log::LogLevel l) noexcept
	{
		switch (l)
		{
			case thx::log::LogLevel::Debug:
				return spdlog::level::debug;
			case thx::log::LogLevel::Info:
				return spdlog::level::info;
			case thx::log::LogLevel::Warn:
				return spdlog::level::warn;
			case thx::log::LogLevel::Error:
				return spdlog::level::err;
			default:
				return spdlog::level::info;
		}
	}

	// spdlog-backed implementation of the framework logging service. All
	// spdlog/fmt machinery is contained here — none of it crosses the ABI
	// boundary (the only types on write() are ABI-stable: LogLevel, the
	// SourceLocation C strings, and StringView).
	struct SpdlogService : thx::log::ILogService
	{
		std::shared_ptr<spdlog::logger> m_logger;

		SpdlogService()
			: m_logger(std::make_shared<spdlog::logger>(
				  "thorax", std::make_shared<spdlog::sinks::stderr_color_sink_mt>()))
		{
			// trace so every level the framework emits is passed through; the
			// framework already decides what is worth logging.
			m_logger->set_level(spdlog::level::trace);
			m_logger->set_pattern("[thorax] [%l] %v");
		}

		void write(thx::log::LogRecord const& r) override
		{
			// r.message is a non-owning view valid only for this call; fmt copies
			// what it needs synchronously.
			std::string_view msg(r.message.data(), r.message.size());
			m_logger->log(toSpd(r.level), "{}:{} {}: {}",
						  r.location.file, r.location.line, r.location.function, msg);
			m_logger->flush();
		}
	};

	// Custom IPlugin so the logger carries its own name/version (its selection
	// identity) distinct from the interface id it registers under. SpdlogService
	// registers under thx.log.ILogService (inherited via Service<ILogService>),
	// which is what the log facade looks up; the plugin advertises that id in
	// provides() so pluginsProviding<ILogService>() discovers it by name.
	struct SpdlogPlugin : thx::plugin::IPlugin
	{
		thx::StringView name() const override { return "thx.spdlog.SpdlogService"; }
		thx::Version version() const override { return thx::Version{1, 0, 0}; }

		bool onLoad() override { return thx::service::registerService<SpdlogService>(); }
		void onUnload() override { thx::service::unregisterService<SpdlogService>(); }

		thx::Span<const thx::service::ServiceID> provides() const override
		{
			static const thx::service::ServiceID kProvides[] = {thx::log::ILogService::staticId()};
			return {kProvides, 1};
		}
	};

} // namespace

THX_DEFINE_PLUGIN(SpdlogPlugin)
