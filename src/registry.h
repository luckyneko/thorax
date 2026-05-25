/*
 *  Created by LuckyNeko on 22/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "plugin/plugin_garbage.h"
#include "plugin/plugin_manager.h"
#include "thx/service/service_manager.h"
#include "thx/thx_api.h"

#include <string>

namespace thx
{
	// Top-level container that owns the framework's process-wide state.
	//
	// Registry owns:
	//   - the PluginGarbage queue (deferred-dlclose queue for plugin DSOs);
	//   - the ServiceManager (process-wide service registry);
	//   - the PluginManager (loaded plugins, indexed by canonical path).
	//
	// All three members are accessible by reference and have stable addresses
	// across the lifetime of the singleton — callers may take and hold
	// references freely. The class is intentionally non-copyable / non-movable.
	//
	// Registry::instance() is the framework's only static singleton; the
	// individual manager classes no longer expose their own instance()
	// accessors. Tests that need isolated state continue to construct local
	// ServiceManager / PluginManager instances directly.
	class THX_API Registry
	{
	public:
		Registry(Registry const&)            = delete;
		Registry& operator=(Registry const&) = delete;
		Registry(Registry&&)                 = delete;
		Registry& operator=(Registry&&)      = delete;

		// Process-wide singleton accessor. Constructs lazily on first call.
		static Registry& instance() noexcept;

		thx::service::ServiceManager& serviceManager() noexcept { return m_serviceManager; }
		thx::plugin::PluginManager&   pluginManager()  noexcept { return m_pluginManager;  }
		thx::plugin::PluginGarbage&   pluginGarbage()  noexcept { return m_pluginGarbage;  }

		// Optional human-readable name set via thx::initialise(). Used for
		// diagnostics; has no effect on framework behaviour. Empty until
		// initialise() is called.
		std::string const& debugName() const noexcept { return m_debugName; }

	private:
		Registry();

		// Allow the free-function lifecycle hooks to mutate state without
		// exposing it on the public surface.
		friend bool initialise(std::string debugName);
		friend void shutdown() noexcept;

		// Declaration (= initialisation) order matters in two ways:
		//   - m_pluginGarbage must be initialised first so it outlives both
		//     m_serviceManager and m_pluginManager: at destruction, ~PluginManager
		//     schedules each plugin's DSO into the queue, so the queue must
		//     still be alive at that point.
		//   - m_serviceManager must be initialised before m_pluginManager
		//     because m_pluginManager's constructor takes m_serviceManager by
		//     reference.
		thx::plugin::PluginGarbage    m_pluginGarbage;
		thx::service::ServiceManager  m_serviceManager;
		thx::plugin::PluginManager    m_pluginManager;
		std::string                   m_debugName;
	};

	// Free-function lifecycle for the Registry singleton.
	//
	// initialise() records an optional human-readable name on the Registry.
	// The Registry itself is lazily constructed by Registry::instance() on
	// first use — calling initialise() is not required to use the framework.
	//
	// Returns true if this call set the debug name (i.e. it was empty before),
	// false if a previous initialise() already set one.
	THX_API bool initialise(std::string debugName);

	// shutdown() drains the deferred-close queue. It does NOT destroy the
	// Registry — the singleton persists until program exit. Safe to call
	// multiple times; equivalent to thx::plugin::collectGarbage() followed by
	// clearing the debug name.
	//
	// Safety: callers MUST release any shared_ptr<IService> references into
	// unloaded DSOs before calling shutdown(). See plugin_garbage.h.
	THX_API void shutdown() noexcept;

	// Shorthand for Registry::instance(). Exists so callers don't have to type
	// the class name; behaves identically.
	THX_API Registry& registry() noexcept;

} // namespace thx
