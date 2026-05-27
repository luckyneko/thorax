/*
 *  Created by LuckyNeko on 22/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "thx/service/iservice.h"
#include "thx/service/service.h"   // ServiceFactory, ServiceInfo
#include "thx/thx_api.h"

#include <memory>
#include <mutex>
#include <type_traits>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace thx::service
{

	// Central registry that owns the lifetime of all registered services.
	//
	// Thread safety: concurrent getService() calls do not block each other
	// (shared lock). register/unregister take an exclusive lock.
	//
	// Single-owner semantics: a given ServiceID may be registered exactly once.
	// A second registerService call for the same ID returns false with a
	// diagnostic — the registry rejects duplicate ownership rather than
	// silently sharing it. Plugins that want to *contribute* to an existing
	// service (rather than replace it) should use the provider pattern
	// exposed by the relevant service interface.
	class THX_API ServiceManager
	{
	public:
		ServiceManager() = default;
		~ServiceManager() = default;

		ServiceManager(ServiceManager const&) = delete;
		ServiceManager& operator=(ServiceManager const&) = delete;

		// Registers a service by ID, version, and a factory.
		//
		// The factory's invoke() is called exactly once; the returned raw
		// IService* is wrapped in shared_ptr<IService> and stored as the sole
		// registered instance. The factory's destroyCtx() runs exactly once
		// regardless of outcome (success, factory-returned-null, ID already
		// reserved, throw).
		//
		// After construction, IService::onConstruct() is called. If it returns
		// false the service is discarded and registration fails.
		//
		// Returns false and logs a diagnostic if:
		//   - factory.invoke is null or returns null
		//   - onConstruct() returns false
		//   - the ID is already registered (regardless of version)
		bool registerService(ServiceID id, Version version, ServiceFactory factory);

		// Convenience overload: accepts any callable returning IService*, wraps
		// it via makeServiceFactory(). Mostly used by tests; production code
		// goes through the facade in service.h.
		template <typename Callable, typename = std::enable_if_t<
			!std::is_same_v<std::decay_t<Callable>, ServiceFactory>>>
		bool registerService(ServiceID id, Version version, Callable&& callable)
		{
			return registerService(std::move(id), version,
			    makeServiceFactory(std::forward<Callable>(callable)));
		}

		// Type-deducing registration. Requires T to provide T::staticId() and
		// T::staticVersion(). Most callers should use the facade in service.h
		// instead — that hides the ServiceFactory construction entirely.
		template <typename T>
		bool registerService(ServiceFactory factory);

		// Type-deducing registration with an arbitrary callable. Wraps the
		// callable via makeServiceFactory().
		template <typename T, typename Callable, typename = std::enable_if_t<
			!std::is_same_v<std::decay_t<Callable>, ServiceFactory>>>
		bool registerService(Callable&& callable)
		{
			return registerService<T>(makeServiceFactory(std::forward<Callable>(callable)));
		}

		// Looks up a service by ID and casts it to T.
		// Returns nullptr if the service is not registered or the cast fails.
		template <typename T>
		std::shared_ptr<T> getService(ServiceID id) const;

		// Type-deducing overload. Requires T to provide T::staticId().
		template <typename T>
		std::shared_ptr<T> getService() const;

		// Removes the service entry. IService::onDestroy() is called outside
		// the registry lock so the service may safely call ServiceManager
		// during shutdown.
		//
		// Returns true if the entry was removed.
		// Returns false (and logs a diagnostic) if the ID is not registered.
		bool unregisterService(ServiceID id);

		// Type-deducing unregister. Requires T to provide T::staticId().
		template <typename T>
		bool unregisterService();

		// Returns a point-in-time snapshot of all registered service IDs.
		// Useful for diagnostics and test assertions.
		std::vector<ServiceInfo> listServices() const;

	private:
		mutable std::shared_mutex m_mutex;
		std::unordered_map<ServiceID, std::shared_ptr<IService>> m_services;
		// IDs reserved by an in-flight registerService. The factory and
		// onConstruct callback run without the registry lock held; the ID is
		// kept here so concurrent registers see it as taken and bail out.
		std::unordered_set<ServiceID> m_reserved;
	};
} // namespace thx::service
#include "service/service_manager.inl"
