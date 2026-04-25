/*
 *  Created by LuckyNeko on 25/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "thx/iservice.h"
#include "thx/service_manager.h"
#include "thx/span.h"
#include "thx/string_view.h"
#include "thx/version_type.h"

#include <memory>
#include <new>

namespace thx
{
	// Plugin abstraction. A DSO produces exactly one IPlugin via thx_create_plugin
	// and may register any number of services (or none) in onLoad.
	//
	// Lifetime: PluginLoader instantiates the IPlugin via the DSO's thx_create_plugin
	// export, then calls onLoad. On unload, onUnload runs first, then the IPlugin is
	// destroyed via the DSO's thx_destroy_plugin export.
	class IPlugin
	{
	public:
		virtual ~IPlugin() = default;

		// Human-readable plugin name. Used for diagnostics.
		virtual StringView name() const = 0;

		// The plugin's own version (independent of any service it registers).
		virtual Version version() const = 0;

		// Called by PluginLoader after the DSO is loaded but before the plugin
		// becomes visible to callers. Register any services here. Return false
		// to abort the load; the plugin will be destroyed and the DSO closed
		// without becoming visible.
		virtual bool onLoad(ServiceManager& sm) = 0;

		// Called by PluginLoader before the DSO is closed. Unregister any
		// services registered in onLoad.
		virtual void onUnload(ServiceManager& sm) = 0;

		// Optional: services that must already be registered before onLoad runs.
		// PluginLoader rejects the load if any are missing. Default: no requirements.
		virtual Span<const ServiceID> required() const { return {}; }
	};

	// Convenience IPlugin used by THX_DEFINE_SERVICE_PLUGIN. Registers exactly
	// one service of type T in onLoad and unregisters it in onUnload.
	//
	// T must satisfy:
	//   - inherits from thx::Service<T> (provides static_id() and id()/version())
	//   - provides static constexpr Version static_version()
	//   - is default-constructible
	template <typename T>
	class ServicePluginShim : public IPlugin
	{
	public:
		StringView name() const override { return T::static_id().name(); }
		Version    version() const override { return T::static_version(); }

		bool onLoad(ServiceManager& sm) override
		{
			return sm.template register_service<T>([]() -> std::shared_ptr<IService>
			{
				return std::make_shared<T>();
			});
		}

		void onUnload(ServiceManager& sm) override
		{
			sm.template unregister_service<T>();
		}
	};

} // namespace thx
