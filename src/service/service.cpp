/*
 *  Created by LuckyNeko on 25/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "thx/service/service.h"
#include "registry.h"

namespace thx::service::detail
{

bool registerServiceImpl(ServiceID id, Version version, ServiceFactory factory)
{
	return thx::Registry::instance()
	    .serviceManager()
	    .registerService(std::move(id), version, std::move(factory));
}

bool unregisterServiceImpl(ServiceID id)
{
	return thx::Registry::instance()
	    .serviceManager()
	    .unregisterService(std::move(id));
}

std::shared_ptr<IService> getServiceImpl(ServiceID id)
{
	return thx::Registry::instance()
	    .serviceManager()
	    .getService<IService>(std::move(id));
}

std::vector<ServiceInfo> listServicesImpl()
{
	return thx::Registry::instance().serviceManager().listServices();
}

} // namespace thx::service::detail
