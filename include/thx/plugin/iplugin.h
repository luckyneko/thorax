/*
 *  Created by LuckyNeko on 25/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "thx/service/iservice.h"
#include "thx/service/service_manager.h"
#include "thx/span.h"
#include "thx/string_view.h"
#include "thx/version_type.h"

#include <memory>
#include <new>

namespace thx::plugin
{
	// Pairs a ServiceID with a minimum acceptable Version. Used by
	// IPlugin::required() to declare versioned dependencies.
	struct ServiceRequirement
	{
		thx::service::ServiceID id;
		Version                 version;
	};

	// Plugin abstraction. A DSO produces exactly one IPlugin via thx_create_plugin
	// and may register any number of services (or none) in onLoad.
	//
	// Lifetime: callers may hold shared_ptr<IService> handles past unload —
	// PluginManager::unload defers the dlclose into a graveyard that's drained
	// at the next load() or via thx::collectPluginGarbage(). Don't drain
	// while service references are still alive: their destructors live in
	// plugin code and need the DSO mapped to run.
	class IPlugin
	{
	public:
		virtual ~IPlugin() = default;

		// Human-readable plugin name. Used for diagnostics.
		virtual StringView name() const = 0;

		// The plugin's own version (independent of any service it registers).
		virtual Version version() const = 0;

		// Called by PluginManager after the DSO is loaded but before the plugin
		// becomes visible to callers. Register any services here. Return false
		// to abort the load; the plugin will be destroyed and the DSO closed
		// without becoming visible.
		virtual bool onLoad(thx::service::ServiceManager& sm) = 0;

		// Called by PluginManager before the DSO is closed. Unregister any
		// services registered in onLoad.
		virtual void onUnload(thx::service::ServiceManager& sm) = 0;

		// Optional: services that must already be registered (at a sufficient
		// version) before onLoad runs. PluginManager rejects the load if any
		// requirement is missing or the registered version is too old.
		// Default: no requirements.
		virtual Span<const ServiceRequirement> required() const { return {}; }
	};

	// Convenience IPlugin used by THX_DEFINE_SERVICE_PLUGIN. Registers exactly
	// one service of type T in onLoad and unregisters it in onUnload.
	//
	// T must satisfy:
	//   - inherits from thx::service::Service<T> (provides staticId() and id()/version())
	//   - provides static constexpr Version staticVersion()
	//   - is default-constructible
	template <typename T>
	class ServicePluginShim : public IPlugin
	{
	public:
		StringView name() const override { return T::staticId().name(); }
		Version    version() const override { return T::staticVersion(); }

		bool onLoad(thx::service::ServiceManager& sm) override
		{
			return sm.template registerService<T>([]() -> std::shared_ptr<thx::service::IService>
			{
				return std::make_shared<T>();
			});
		}

		void onUnload(thx::service::ServiceManager& sm) override
		{
			sm.template unregisterService<T>();
		}
	};

} // namespace thx::plugin
