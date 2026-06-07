/*
 *  Created by LuckyNeko on 22/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "registry.h"

namespace thx
{

	Registry::Registry(Settings settings)
		: m_name(settings.name)
		, m_pluginGarbage()
		, m_serviceManager()
		, m_pluginManager(m_serviceManager)
	{
	}

	Registry::~Registry()
	{
		clear();
	}

	void Registry::clear() noexcept
	{
		// Tear framework-owned state down to empty, in order:
		//   1. unload every plugin (runs onUnload, unregisters their services,
		//      queues each DSO into PluginGarbage);
		//   2. unregister any services registered directly (not owned by a plugin),
		//      running their onDestroy;
		//   3. drain the deferred-close queue so the DSOs queued in step 1 are
		//      actually unmapped;
		//   4. clear the debug name.
		// After this call no framework-owned services, loaded plugins, or mapped
		// plugin DSOs survive. Callers MUST have released every ServiceHandle into
		// a plugin DSO first (step 3 dlclose's them). A plugin's onUnload may call
		// back through the facade into registry(), so this runs in place — the
		// Registry (and the global registry() pointer) stays live throughout.
		m_pluginManager.clear();
		m_serviceManager.clear();
		m_pluginGarbage.collect();
		m_name.clear();
	}
} // namespace thx
