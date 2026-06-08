/*
 *  Created by LuckyNeko on 22/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "thx/thorax.h"

#include "plugin/plugin_garbage.h"
#include "plugin/plugin_manager.h"
#include "service/service_manager.h"
#include "thx_internal_api.h"

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
	// All members are accessible by reference and have stable addresses
	// across the lifetime of the singleton — callers may take and hold
	// references freely. The class is intentionally non-copyable / non-movable.
	//
	// Registry::instance() is the framework's only static singleton; the
	// individual manager classes no longer expose their own instance()
	// accessors. Tests isolate state by resetting the singleton between cases
	// (thx::shutdown() via a Catch2 listener) rather than by constructing
	// alternate registries.
	class THX_INTERNAL_API Registry
	{
	public:
		Registry(Settings settings);
		~Registry();
		Registry(const Registry&) = delete;
		Registry& operator=(const Registry&) = delete;
		Registry(Registry&&) = delete;
		Registry& operator=(Registry&&) = delete;

		thx::service::ServiceManager& serviceManager() noexcept { return m_serviceManager; }
		thx::plugin::PluginManager& pluginManager() noexcept { return m_pluginManager; }
		thx::plugin::PluginGarbage& pluginGarbage() noexcept { return m_pluginGarbage; }

		// Tears the owned state down to empty *in place* — unloads every plugin,
		// unregisters every remaining service, and drains the deferred-close
		// queue — without destroying the Registry itself. This is the body of
		// ~Registry, and the first half of thx::shutdown(): shutdown() runs it
		// while the global registry() pointer is still valid (a plugin's onUnload
		// reaches back through the facade to registry()), then destroys the
		// now-empty shell. Idempotent — a second call has nothing to do.
		void clear() noexcept;

		// Optional human-readable name set via thx::initialise(). Used for
		// diagnostics; has no effect on framework behaviour. Empty until
		// initialise() is called.
		const std::string& name() const noexcept { return m_name; }

	private:
		// Declaration (= initialisation) order matters in two ways:
		//   - m_pluginGarbage must be initialised first so it outlives both
		//     m_serviceManager and m_pluginManager: at destruction, ~PluginManager
		//     schedules each plugin's DSO into the queue, so the queue must
		//     still be alive at that point.
		//   - m_serviceManager must be initialised before m_pluginManager
		//     because m_pluginManager's constructor takes m_serviceManager by
		//     reference.
		std::string m_name;
		thx::plugin::PluginGarbage m_pluginGarbage;
		thx::service::ServiceManager m_serviceManager;
		thx::plugin::PluginManager m_pluginManager;
	};
} // namespace thx
