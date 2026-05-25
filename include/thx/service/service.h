/*
 *  Created by LuckyNeko on 22/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "thx/service/iservice.h"
#include "thx/thx_api.h"
#include "thx/version_type.h"

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

// Public service-layer surface. Free functions forward to the framework's
// internal ServiceManager (which is an implementation detail in src/). Plugin
// authors use these from inside IPlugin::onLoad / onUnload; while those hooks
// are executing, the facade routes through the PluginManager's active
// ServiceManager (the Registry-owned one in production, or a caller-supplied
// one when constructing a local PluginManager in tests).

namespace thx::service
{
	// Factory callable type used by registerService.
	using ServiceFactory = std::function<std::shared_ptr<IService>()>;

	// Snapshot entry returned by listServices().
	struct ServiceInfo
	{
		ServiceID id;
		Version   version;
	};

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
		static_assert(std::is_base_of_v<IService, T>,
		    "getService<T>: T must derive from thx::service::IService");
		// See note in service_manager.inl re: static vs dynamic cast across DSOs.
		auto base = detail::getServiceImpl(std::move(id));
		return base ? std::static_pointer_cast<T>(std::move(base)) : nullptr;
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
