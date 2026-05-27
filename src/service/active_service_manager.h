/*
 *  Created by LuckyNeko on 26/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "thx_internal_api.h"

// Thread-local override hook for the thx::service::* facades.
//
// In production, the facades dispatch through Registry::instance().serviceManager().
// During PluginManager::load/unload, we temporarily swap a different
// ServiceManager into the thread-local slot so that an IPlugin's onLoad/
// onUnload — which now takes no parameter and uses the facade — registers
// against the loading PluginManager's m_sm rather than the Registry's. This
// preserves the local-PluginManager / local-ServiceManager test isolation
// pattern that test_plugin_manager.cpp relies on.
//
// Outside the load/unload window, no override is set and the facades fall
// through to Registry.

namespace thx::service
{
	class ServiceManager;
}

namespace thx::service::detail
{
	// Returns the active override if PluginManager has set one, otherwise null.
	THX_INTERNAL_API ServiceManager* activeServiceManager() noexcept;

	// RAII scope helper used by PluginManager to install m_sm as the active
	// override around plugin onLoad/onUnload calls. Restores the previous
	// override (which is normally null) on destruction.
	class THX_INTERNAL_API ActiveServiceManagerScope
	{
	public:
		explicit ActiveServiceManagerScope(ServiceManager& sm) noexcept;
		~ActiveServiceManagerScope();

		ActiveServiceManagerScope(ActiveServiceManagerScope const&)            = delete;
		ActiveServiceManagerScope& operator=(ActiveServiceManagerScope const&) = delete;

	private:
		ServiceManager* m_previous;
	};

} // namespace thx::service::detail
