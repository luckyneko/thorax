/*
 *  Created by LuckyNeko on 25/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "thx/service/service.h"
#include "registry.h"
#include "service/service_manager.h"

namespace thx::service
{

	// All facade operations target the one process-wide ServiceManager owned by the
	// Registry. (PluginManager registers a plugin's services during onLoad through
	// these same facades; it is constructed with the Registry's ServiceManager, so
	// its m_sm is this same instance.)

	bool registerService(ServiceID id, Version version, ServiceFactory factory)
	{
		return thx::registry()->serviceManager().registerService(std::move(id), version, std::move(factory));
	}

	bool unregisterService(ServiceID id)
	{
		return thx::registry()->serviceManager().unregisterService(std::move(id));
	}

	IService* acquireService(ServiceID id)
	{
		// getService<IService> returns a ServiceHandle<IService> whose ctor already
		// incremented the refcount. detach() hands the raw pointer out without
		// releasing — caller's ServiceHandle::adopt completes the hand-off.
		return thx::registry()->serviceManager().getService<IService>(std::move(id)).detach();
	}

	std::vector<ServiceInfo> listServices()
	{
		return thx::registry()->serviceManager().listServices();
	}

} // namespace thx::service
