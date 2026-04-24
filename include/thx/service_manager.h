/*
 *  Created by LuckyNeko on 22/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "thx/iservice.h"
#include "thx/log.h"

#include <functional>
#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace thx
{

	// Factory callable type used by register_service.
	using ServiceFactory = std::function<std::shared_ptr<IService>()>;

	// Snapshot entry returned by ServiceManager::list_services().
	struct ServiceInfo
	{
		ServiceID id;
		int       ref_count;
	};

	// Central registry that owns the lifetime of all registered services.
	//
	// Thread safety: concurrent get_service() calls do not block each other
	// (shared lock). register/unregister take an exclusive lock.
	//
	// Reference counting: multiple callers may register the same service ID.
	// The entry is only removed when the last registrant calls unregister_service.
	class ServiceManager
	{
	public:
		ServiceManager() = default;
		~ServiceManager() = default;

		ServiceManager(ServiceManager const&) = delete;
		ServiceManager& operator=(ServiceManager const&) = delete;

		// Returns the process-wide singleton instance.
		static ServiceManager& instance();

		// Replaces the global log sink used by all thorax diagnostics.
		// Equivalent to calling thx::set_log_sink() directly.
		static void set_log_sink(std::shared_ptr<ILogSink> sink);

		// Registers a service by ID, version, and a factory callable.
		//
		// The factory is called only when the service is not yet registered,
		// ensuring at most one instance exists at any time. If a compatible
		// version is already registered the ref count is incremented and the
		// factory is never invoked.
		//
		// After construction, IService::onConstruct() is called. If it returns
		// false the service is discarded and registration fails.
		//
		// Returns false and logs a diagnostic if:
		//   - factory is null or returns null
		//   - onConstruct() returns false
		//   - an incompatible version is already registered for that ID
		bool register_service(ServiceID id, Version version, ServiceFactory factory);

		// Type-deducing registration. Requires T to provide T::static_id() and
		// T::static_version(). The factory must return a std::shared_ptr<T> (or
		// any type implicitly convertible to std::shared_ptr<IService>).
		template <typename T>
		bool register_service(ServiceFactory factory);

		// Looks up a service by ID and casts it to T.
		// Returns nullptr if the service is not registered or the cast fails.
		template <typename T>
		std::shared_ptr<T> get_service(ServiceID id) const;

		// Type-deducing overload. Requires T to provide T::static_id().
		template <typename T>
		std::shared_ptr<T> get_service() const;

		// Decrements the ref count for the given service.
		// The entry is removed when the count reaches zero.
		//
		// Returns true if the entry was fully removed.
		// Returns false (and logs a diagnostic) if the ID is not registered.
		bool unregister_service(ServiceID id);

		// Type-deducing unregister. Requires T to provide T::static_id().
		template <typename T>
		bool unregister_service();

		// Returns a point-in-time snapshot of all registered service IDs and
		// their reference counts. Useful for diagnostics and test assertions.
		std::vector<ServiceInfo> list_services() const;

	private:
		struct Entry
		{
			std::shared_ptr<IService> service;
			int ref_count{1};
		};

		mutable std::shared_mutex mutex_;
		std::unordered_map<ServiceID, Entry> services_;
	};

	// ---------------------------------------------------------------------------
	// Template implementation
	// ---------------------------------------------------------------------------

	template <typename T>
	std::shared_ptr<T> ServiceManager::get_service(ServiceID id) const
	{
		std::shared_lock lock(mutex_);
		auto it = services_.find(id);
		if (it == services_.end())
			return nullptr;
		return std::dynamic_pointer_cast<T>(it->second.service);
	}

	template <typename T>
	std::shared_ptr<T> ServiceManager::get_service() const
	{
		return get_service<T>(T::static_id());
	}

	template <typename T>
	bool ServiceManager::register_service(ServiceFactory factory)
	{
		return register_service(T::static_id(), T::static_version(), std::move(factory));
	}

	template <typename T>
	bool ServiceManager::unregister_service()
	{
		return unregister_service(T::static_id());
	}

} // namespace thx
