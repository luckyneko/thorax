/*
 *  Created by LuckyNeko on 22/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "thx/service/iservice.h"
#include "thx/service/service_manager.h"  // ServiceFactory, ServiceInfo
#include "thx/thx_api.h"
#include "thx/version_type.h"

#include <memory>
#include <utility>
#include <vector>

// Public service-layer surface. Free functions forward to the framework's
// internal ServiceManager (owned by the process-wide Registry singleton).
// Callers that already hold a ServiceManager& (e.g. inside IPlugin::onLoad)
// can keep calling its methods directly — these facades exist so code with no
// ServiceManager& in scope doesn't have to reach for the Registry by hand.

namespace thx::service
{
	// --- Detail: exported impls the templates dispatch through ------------
	// Public consumers should call the template wrappers below, not these.
	namespace detail
	{
		THX_API bool                      registerServiceImpl(ServiceID id, Version version, ServiceFactory factory);
		THX_API bool                      unregisterServiceImpl(ServiceID id);
		THX_API std::shared_ptr<IService> getServiceImpl(ServiceID id);
		THX_API std::vector<ServiceInfo>  listServicesImpl();
	}

	// --- Facade -----------------------------------------------------------

	inline bool registerService(ServiceID id, Version version, ServiceFactory factory)
	{
		return detail::registerServiceImpl(std::move(id), version, std::move(factory));
	}

	template <typename T>
	inline bool registerService(ServiceFactory factory)
	{
		return detail::registerServiceImpl(T::staticId(), T::staticVersion(), std::move(factory));
	}

	inline bool unregisterService(ServiceID id)
	{
		return detail::unregisterServiceImpl(std::move(id));
	}

	template <typename T>
	inline bool unregisterService()
	{
		return detail::unregisterServiceImpl(T::staticId());
	}

	template <typename T>
	inline std::shared_ptr<T> getService(ServiceID id)
	{
		return std::dynamic_pointer_cast<T>(detail::getServiceImpl(std::move(id)));
	}

	template <typename T>
	inline std::shared_ptr<T> getService()
	{
		return getService<T>(T::staticId());
	}

	inline std::vector<ServiceInfo> listServices()
	{
		return detail::listServicesImpl();
	}

} // namespace thx::service
