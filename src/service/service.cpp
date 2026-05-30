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

namespace thx::service::detail
{

namespace
{
	// All facade operations target the one process-wide ServiceManager owned by
	// the Registry. (PluginManager registers a plugin's services during onLoad
	// through these same facades; it is constructed with the Registry's
	// ServiceManager, so its m_sm is this same instance.)
	ServiceManager& targetSM() noexcept
	{
		return thx::Registry::instance().serviceManager();
	}
}

bool registerServiceImpl(ServiceID id, Version version, ServiceFactory factory)
{
	return targetSM().registerService(std::move(id), version, std::move(factory));
}

bool unregisterServiceImpl(ServiceID id)
{
	return targetSM().unregisterService(std::move(id));
}

IService* acquireServiceImpl(ServiceID id)
{
	// targetSM().getService<IService> returns a ServiceHandle<IService> whose
	// ctor already incremented the refcount. detach() hands the raw pointer
	// out without releasing — caller's ServiceHandle::adopt completes the
	// hand-off.
	return targetSM().getService<IService>(std::move(id)).detach();
}

std::vector<ServiceInfo> listServicesImpl()
{
	return targetSM().listServices();
}

} // namespace thx::service::detail
