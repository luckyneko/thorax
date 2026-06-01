/*
 *  Created by LuckyNeko on 22/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "registry.h"
#include "thx/lifecycle.h"

namespace thx
{

	Registry::Registry()
		: m_pluginManager(m_serviceManager)
	{
	}

	Registry& Registry::instance() noexcept
	{
		static Registry inst;
		return inst;
	}

	bool initialise(std::string debugName)
	{
		auto& reg = Registry::instance();
		if (!reg.m_debugName.empty())
			return false;
		reg.m_debugName = std::move(debugName);
		return true;
	}

	void shutdown() noexcept
	{
		auto& reg = Registry::instance();
		// Tear framework-owned state down in the same order ~Registry would:
		//   1. unload every plugin (runs onUnload, unregisters their services,
		//      queues each DSO into PluginGarbage);
		//   2. unregister any services registered directly (not owned by a plugin),
		//      running their onDestroy;
		//   3. drain the deferred-close queue so the DSOs queued in step 1 are
		//      actually unmapped;
		//   4. clear the debug name.
		// After this call no framework-owned services, loaded plugins, or mapped
		// plugin DSOs survive — only the empty Registry shell persists. Callers
		// MUST have released every ServiceHandle into a plugin DSO first (step 3
		// dlclose's them).
		reg.m_pluginManager.clear();
		reg.m_serviceManager.clear();
		reg.m_ioService.clear();
		reg.m_pluginGarbage.collect();
		reg.m_debugName.clear();
	}

	Registry& registry() noexcept
	{
		return Registry::instance();
	}

} // namespace thx
