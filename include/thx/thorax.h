/*
 *  Created by LuckyNeko on 16/03/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "thx/plugin/plugin.h"
#include "thx/service/service.h"
#include "thx/version.h"

namespace thx
{
	struct Settings
	{
		std::string name = "thorax";
	};
	// Records an optional human-readable name on the framework's internal state.
	// Returns true if this call set the name, false if a previous initialise()
	// already did.
	THX_API bool initialise(Settings settings = Settings());

	// Tears the framework's owned state down to empty: unloads every loaded
	// plugin (running each IPlugin::onUnload), unregisters every remaining
	// service (running each IService::onDestroy), drains the deferred-close
	// queue (equivalent to thx::plugin::collectGarbage()), and clears the debug
	// name. After this call no framework-owned services, loaded plugins, or
	// mapped plugin DSOs survive — only the empty Registry shell persists (the
	// singleton itself is NOT destroyed). Idempotent and safe to call multiple
	// times.
	//
	// Safety: callers MUST release any ServiceHandle<IService> references into
	// plugin DSOs before invoking this — shutdown() dlclose's those DSOs, and a
	// later release of a dangling handle runs the service destructor in unmapped
	// code.
	THX_API void shutdown() noexcept;

	// For Internal Use
	class Registry;
	THX_API Registry* registry() noexcept;

} // namespace thx
