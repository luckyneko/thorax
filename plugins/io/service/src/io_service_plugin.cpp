/*
 *  Created by LuckyNeko on 01/06/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

// The io provider plugin. Registers a single IIoService — the scheme dispatcher
// — under the stable id "thx.io.IIoService"; the io facade (thx::io::open)
// resolves it via getService<IIoService>(), and handler plugins (file, http)
// contribute IProtocol handlers to it via addHandler(). It ships NO built-in
// handler: the dispatcher is a pure router, every scheme comes from a handler
// plugin (file:// included — see plugins/io/file).
//
// Identity split (mirrors the spdlog logger): the *service* registers under the
// interface id thx.io.IIoService (what the facade looks up); the *plugin's own*
// name is its manifest identity, "thx.io.IoService", which provides() advertises
// so a host can discover the dispatcher provider by name.

#include <thx/io/io_service.h>
#include <thx/log/log.h>
#include <thx/plugin/platform.h>
#include <thx/service/service.h>

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace
{
	using namespace thx;	 // Result, Error, ErrorCode, Span, StringView, Version
	using namespace thx::io; // IIoService, IProtocol, StreamHandle, Mode

	// The scheme of "scheme://rest"; a bare path with no "://" is "file".
	std::string schemeOf(StringView address)
	{
		std::string s(address.data(), address.size());
		auto pos = s.find("://");
		if (pos == std::string::npos)
			return "file";
		return s.substr(0, pos);
	}

	// The dispatcher. Holds the contributed IProtocol handlers (weak — evicted
	// when their owning plugin drops its shared_ptr) indexed by scheme, and
	// routes open() to the handler serving the address's scheme.
	//
	// Thread-safe: a coarse mutex guards the scheme index; open() releases it
	// before dispatching so a handler may call back in.
	class IoServiceImpl : public thx::io::IIoService
	{
	public:
		Result<StreamHandle, thx::io::Error> open(StringView address, Mode mode) override
		{
			std::string scheme = schemeOf(address);

			std::shared_ptr<IProtocol> handler;
			{
				std::lock_guard lock(m_mutex);
				auto it = m_byScheme.find(scheme);
				if (it != m_byScheme.end())
				{
					handler = it->second.lock();
					if (!handler)
						m_byScheme.erase(it); // cull expired
				}
			}

			if (!handler)
				return Result<StreamHandle, thx::io::Error>::err(
					{thx::io::ErrorCode::NoHandler, "no I/O handler registered for scheme '" + scheme + "'"});

			// Dispatch without the lock held so a handler may call back in.
			return handler->open(address, mode);
		}

		bool addHandler(std::shared_ptr<IProtocol> handler) override
		{
			if (!handler)
				return false;
			std::lock_guard lock(m_mutex);
			for (auto scheme : handler->schemes())
			{
				std::string key(scheme.data(), scheme.size());
				auto it = m_byScheme.find(key);
				if (it != m_byScheme.end() && !it->second.expired())
				{
					thx::log::warn("io: scheme '" + key + "' already has a handler; ignoring duplicate");
					continue;
				}
				m_byScheme[key] = handler;
			}
			return true;
		}

		void removeHandler(IProtocol* handler) override
		{
			std::lock_guard lock(m_mutex);
			for (auto it = m_byScheme.begin(); it != m_byScheme.end();)
			{
				auto sp = it->second.lock();
				if (!sp || sp.get() == handler)
					it = m_byScheme.erase(it);
				else
					++it;
			}
		}

	private:
		std::mutex m_mutex;
		// scheme -> handler (weak: handlers live as long as their plugin).
		std::unordered_map<std::string, std::weak_ptr<IProtocol>> m_byScheme;
	};

	// Custom IPlugin so the dispatcher carries its own manifest name (its
	// selection identity) distinct from the interface id it registers under.
	// IoServiceImpl registers under thx.io.IIoService (inherited via
	// Service<IIoService>), which is what thx::io::open() looks up; the plugin
	// advertises that id in provides() so pluginsProviding<IIoService>()
	// discovers it by name.
	struct IoServicePlugin : thx::plugin::IPlugin
	{
		thx::StringView name() const override { return "thx.io.IoService"; }
		thx::Version version() const override { return thx::Version{1, 0, 0}; }

		bool onLoad() override { return thx::service::registerService<IoServiceImpl>(); }
		void onUnload() override { thx::service::unregisterService<IoServiceImpl>(); }

		thx::Span<const thx::service::ServiceID> provides() const override
		{
			static const thx::service::ServiceID kProvides[] = {thx::io::IIoService::staticId()};
			return {kProvides, 1};
		}
	};

} // namespace

THX_DEFINE_PLUGIN(IoServicePlugin)
