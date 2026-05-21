/*
 *  Created by LuckyNeko on 22/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "thx/service_manager.h"
#include "thx/to_string.h"
#include "thx/log.h"

namespace thx
{

ServiceManager& ServiceManager::instance()
{
	static ServiceManager inst;
	return inst;
}

bool ServiceManager::register_service(ServiceID id, Version version, ServiceFactory factory)
{
	if (!factory)
	{
		// No reservation taken yet — nothing to release here.
		thx::log(LogLevel::Error,
		    std::string("register_service: null factory for '") + id.name() + "'");
		return false;
	}

	// Phase 1: reserve the ID. If anyone else already owns it (registered or
	// in-flight reservation) we bail before doing real work.
	{
		std::unique_lock lock(mutex_);
		if (services_.find(id) != services_.end() || reserved_.count(id))
		{
			thx::log(LogLevel::Warn,
			    std::string("register_service: '") + id.name()
			    + "' is already registered (single-owner registry)");
			return false;
		}
		reserved_.insert(id);
	}

	// Phase 2: build the service. The lock is NOT held here, so the factory
	// and onConstruct callback may safely call back into ServiceManager
	// (any attempt to re-register `id` will see the reservation and bail).
	//
	// The reservation must be released on every exit path — including the
	// uncaught-exception path, since user-supplied factories and onConstruct
	// callbacks can throw. The try/catch below catches any exception, releases
	// the reservation, and rethrows so the caller still sees the failure.
	auto release_reservation = [&]
	{
		std::unique_lock lock(mutex_);
		reserved_.erase(id);
	};

	std::shared_ptr<IService> service;
	try
	{
		service = factory();
		if (!service)
		{
			release_reservation();
			thx::log(LogLevel::Error,
			    std::string("register_service: factory returned null for '") + id.name() + "'");
			return false;
		}

		// Verify the service reports the version the caller claimed. Mismatch is
		// a programming error: refuse the registration so Release builds notice
		// the same way Debug builds do.
		if (service->version() != version)
		{
			release_reservation();
			thx::log(LogLevel::Error,
			    std::string("register_service: declared version ")
			    + to_string(version)
			    + " does not match service-reported "
			    + to_string(service->version())
			    + " for '" + id.name() + "'");
			return false;
		}

		if (!service->onConstruct())
		{
			release_reservation();
			thx::log(LogLevel::Error,
			    std::string("register_service: onConstruct failed for '") + id.name() + "'");
			return false;
		}
	}
	catch (...)
	{
		release_reservation();
		throw;
	}

	// Phase 3: commit. Replace the reservation with the real entry atomically.
	{
		std::unique_lock lock(mutex_);
		reserved_.erase(id);
		services_.emplace(id, std::move(service));
	}
	return true;
}

bool ServiceManager::unregister_service(ServiceID id)
{
	std::shared_ptr<IService> to_destroy;

	{
		std::unique_lock lock(mutex_);

		auto it = services_.find(id);
		if (it == services_.end())
		{
			thx::log(LogLevel::Warn,
			    std::string("unregister_service: '") + id.name() + "' is not registered");
			return false;
		}

		to_destroy = std::move(it->second);
		services_.erase(it);
	}

	// onDestroy runs without the registry lock so the service may safely call
	// ServiceManager methods during shutdown.
	if (to_destroy)
		to_destroy->onDestroy();
	return true;
}

std::vector<ServiceInfo> ServiceManager::list_services() const
{
	std::shared_lock lock(mutex_);
	std::vector<ServiceInfo> result;
	result.reserve(services_.size());
	for (auto const& [id, svc] : services_)
		result.push_back({id, svc->version()});
	return result;
}

} // namespace thx
