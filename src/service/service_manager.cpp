/*
 *  Created by LuckyNeko on 22/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "thx/service/service_manager.h"
#include "thx/to_string.h"
#include "thx/log.h"

namespace thx
{

bool ServiceManager::registerService(ServiceID id, Version version, ServiceFactory factory)
{
	if (!factory)
	{
		// No reservation taken yet — nothing to release here.
		thx::log(LogLevel::Error,
		    std::string("registerService: null factory for '") + id.name() + "'");
		return false;
	}

	// Phase 1: reserve the ID. If anyone else already owns it (registered or
	// in-flight reservation) we bail before doing real work.
	{
		std::unique_lock lock(m_mutex);
		if (m_services.find(id) != m_services.end() || m_reserved.count(id))
		{
			thx::log(LogLevel::Warn,
			    std::string("registerService: '") + id.name()
			    + "' is already registered (single-owner registry)");
			return false;
		}
		m_reserved.insert(id);
	}

	// Phase 2: build the service. The lock is NOT held here, so the factory
	// and onConstruct callback may safely call back into ServiceManager
	// (any attempt to re-register `id` will see the reservation and bail).
	//
	// The reservation must be released on every exit path — including the
	// uncaught-exception path, since user-supplied factories and onConstruct
	// callbacks can throw. The try/catch below catches any exception, releases
	// the reservation, and rethrows so the caller still sees the failure.
	auto releaseReservation = [&]
	{
		std::unique_lock lock(m_mutex);
		m_reserved.erase(id);
	};

	std::shared_ptr<IService> service;
	try
	{
		service = factory();
		if (!service)
		{
			releaseReservation();
			thx::log(LogLevel::Error,
			    std::string("registerService: factory returned null for '") + id.name() + "'");
			return false;
		}

		// Verify the service reports the version the caller claimed. Mismatch is
		// a programming error: refuse the registration so Release builds notice
		// the same way Debug builds do.
		if (service->version() != version)
		{
			releaseReservation();
			thx::log(LogLevel::Error,
			    std::string("registerService: declared version ")
			    + toString(version)
			    + " does not match service-reported "
			    + toString(service->version())
			    + " for '" + id.name() + "'");
			return false;
		}

		if (!service->onConstruct())
		{
			releaseReservation();
			thx::log(LogLevel::Error,
			    std::string("registerService: onConstruct failed for '") + id.name() + "'");
			return false;
		}
	}
	catch (...)
	{
		releaseReservation();
		throw;
	}

	// Phase 3: commit. Replace the reservation with the real entry atomically.
	{
		std::unique_lock lock(m_mutex);
		m_reserved.erase(id);
		m_services.emplace(id, std::move(service));
	}
	return true;
}

bool ServiceManager::unregisterService(ServiceID id)
{
	std::shared_ptr<IService> to_destroy;

	{
		std::unique_lock lock(m_mutex);

		auto it = m_services.find(id);
		if (it == m_services.end())
		{
			thx::log(LogLevel::Warn,
			    std::string("unregisterService: '") + id.name() + "' is not registered");
			return false;
		}

		to_destroy = std::move(it->second);
		m_services.erase(it);
	}

	// onDestroy runs without the registry lock so the service may safely call
	// ServiceManager methods during shutdown.
	if (to_destroy)
		to_destroy->onDestroy();
	return true;
}

std::vector<ServiceInfo> ServiceManager::listServices() const
{
	std::shared_lock lock(m_mutex);
	std::vector<ServiceInfo> result;
	result.reserve(m_services.size());
	for (auto const& [id, svc] : m_services)
		result.push_back({id, svc->version()});
	return result;
}

} // namespace thx
