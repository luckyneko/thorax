/*
 *  Created by LuckyNeko on 22/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "thx/registry.h"
#include "thx/service/service_manager.h"

#include <memory>
#include <utility>
#include <vector>

// Free-function shims over the Registry-owned ServiceManager. Each function
// is a one-liner forwarding to `thx::registry().serviceManager().method(...)`,
// matching the method name exactly. Callers that already hold a
// ServiceManager& (e.g. inside an IPlugin::onLoad) should keep using the
// member functions directly — the facades exist so that code with no
// ServiceManager& in scope doesn't have to reach for the registry by hand.

namespace thx::service
{
	inline bool registerService(ServiceID id, Version version, ServiceFactory factory)
	{
		return thx::registry().serviceManager()
		    .registerService(std::move(id), version, std::move(factory));
	}

	template <typename T>
	inline bool registerService(ServiceFactory factory)
	{
		return thx::registry().serviceManager()
		    .template registerService<T>(std::move(factory));
	}

	inline bool unregisterService(ServiceID id)
	{
		return thx::registry().serviceManager().unregisterService(std::move(id));
	}

	template <typename T>
	inline bool unregisterService()
	{
		return thx::registry().serviceManager().template unregisterService<T>();
	}

	template <typename T>
	inline std::shared_ptr<T> getService(ServiceID id)
	{
		return thx::registry().serviceManager().template getService<T>(id);
	}

	template <typename T>
	inline std::shared_ptr<T> getService()
	{
		return thx::registry().serviceManager().template getService<T>();
	}

	inline std::vector<ServiceInfo> listServices()
	{
		return thx::registry().serviceManager().listServices();
	}

} // namespace thx::service
