/*
 *  Created by LuckyNeko on 25/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "thx/service/service.h"
#include "registry.h"
#include "service/active_service_manager.h"
#include "service/service_manager.h"

namespace thx::service::detail
{

namespace
{
	// Per-thread override slot. Null in normal operation; set to a local
	// ServiceManager* during IPlugin::onLoad / onUnload by PluginManager so
	// the facade routes registrations to the loading PluginManager's m_sm.
	thread_local ServiceManager* g_activeSM = nullptr;

	ServiceManager& targetSM() noexcept
	{
		return g_activeSM ? *g_activeSM
		                  : thx::Registry::instance().serviceManager();
	}
}

ServiceManager* activeServiceManager() noexcept
{
	return g_activeSM;
}

ActiveServiceManagerScope::ActiveServiceManagerScope(ServiceManager& sm) noexcept
	: m_previous(g_activeSM)
{
	g_activeSM = &sm;
}

ActiveServiceManagerScope::~ActiveServiceManagerScope()
{
	g_activeSM = m_previous;
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
