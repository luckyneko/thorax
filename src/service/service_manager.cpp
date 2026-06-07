/*
 *  Created by LuckyNeko on 22/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "service/service_manager.h"
#include "thx/log.h"
#include "thx/to_string.h"

namespace thx::service
{

	namespace
	{
		// Run the factory's destroyCtx exactly once on every exit path. Used as
		// an RAII guard around the body of registerService.
		struct FactoryCleanup
		{
			ServiceFactory& factory;
			bool active = true;

			~FactoryCleanup() noexcept
			{
				if (active && factory.destroyCtx)
					factory.destroyCtx(factory.ctx);
			}

			void disarm() noexcept { active = false; }
		};
	} // namespace

	bool ServiceManager::registerService(ServiceID id, Version version, ServiceFactory factory)
	{
		// Always run destroyCtx, regardless of outcome. Disarmed only if invoke
		// succeeds and we've made the ctx no longer relevant (which happens once
		// it returns — the captured state was a one-shot construction).
		FactoryCleanup cleanup{factory};

		if (!factory.invoke)
		{
			thx::logMessage(thx::LogLevel::Error,
				std::string("registerService: null factory.invoke for '") + id.name() + "'");
			return false;
		}

		// Phase 1: reserve the ID. If anyone else already owns it (registered or
		// in-flight reservation) we bail before doing real work. The diagnostic
		// is emitted AFTER releasing the lock: the installed log sink may look up
		// a service (the in-tree log bridge forwards to a registered ILogService
		// via getService, taking this ServiceManager's shared lock), which would
		// deadlock against this exclusive lock (shared_mutex is not recursive).
		bool duplicate = false;
		{
			std::unique_lock lock(m_mutex);
			if (m_services.find(id) != m_services.end() || m_reserved.count(id))
				duplicate = true;
			else
				m_reserved.insert(id);
		}
		if (duplicate)
		{
			thx::logMessage(thx::LogLevel::Warn,
				std::string("registerService: '") + id.name() + "' is already registered (single-owner registry)");
			return false;
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

		ServiceHandle<IService> service;
		try
		{
			IService* raw = factory.invoke(factory.ctx);
			if (!raw)
			{
				releaseReservation();
				thx::logMessage(thx::LogLevel::Error,
					std::string("registerService: factory returned null for '") + id.name() + "'");
				return false;
			}
			// Wrap the raw pointer; ServiceHandle's ctor retains, bringing the
			// intrusive refcount from 0 to 1. Virtual destructor on IService
			// handles polymorphic destruction when the last handle drops.
			service = ServiceHandle<IService>(raw);

			// Verify the service reports the version the caller claimed. Mismatch is
			// a programming error: refuse the registration so Release builds notice
			// the same way Debug builds do.
			if (service->version() != version)
			{
				releaseReservation();
				thx::logMessage(thx::LogLevel::Error,
					std::string("registerService: declared version ") + toString(version) + " does not match service-reported " + toString(service->version()) + " for '" + id.name() + "'");
				return false;
			}

			if (!service->onConstruct())
			{
				releaseReservation();
				thx::logMessage(thx::LogLevel::Error,
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
		ServiceHandle<IService> to_destroy;
		bool missing = false;

		{
			std::unique_lock lock(m_mutex);

			auto it = m_services.find(id);
			if (it == m_services.end())
			{
				missing = true;
			}
			else
			{
				to_destroy = std::move(it->second);
				m_services.erase(it);
			}
		}

		// Diagnostic emitted outside the lock (see registerService for why).
		if (missing)
		{
			thx::logMessage(thx::LogLevel::Warn,
				std::string("unregisterService: '") + id.name() + "' is not registered");
			return false;
		}

		// onDestroy runs without the registry lock so the service may safely call
		// ServiceManager methods during shutdown. The handle (which still holds a
		// strong reference) is destroyed at scope exit; external callers holding
		// their own handles keep the service alive past that point.
		if (to_destroy)
			to_destroy->onDestroy();
		return true;
	}

	void ServiceManager::clear()
	{
		std::vector<ServiceHandle<IService>> to_destroy;

		{
			std::unique_lock lock(m_mutex);
			to_destroy.reserve(m_services.size());
			for (auto& [id, svc] : m_services)
				to_destroy.push_back(std::move(svc));
			m_services.clear();
		}

		// onDestroy runs without the registry lock so a service may safely call
		// back into ServiceManager during teardown (mirrors unregisterService).
		// Handles drop at scope exit; external holders keep their service alive
		// past that point.
		for (auto& handle : to_destroy)
			if (handle)
				handle->onDestroy();
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

} // namespace thx::service
