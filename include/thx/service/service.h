/*
 *  Created by LuckyNeko on 22/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "thx/service/iservice.h"
#include "thx/service/service_factory.h"
#include "thx/service/service_handle.h"
#include "thx/thx_api.h"
#include "thx/version_type.h"

#include <cstddef>
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
//
// The wire ABI between the calling DSO and libthorax is C-compatible by
// design: ServiceFactory is a POD struct of function pointers + a void*;
// ServiceHandle<T> is a single-pointer wrapper around an intrusive refcount
// in IService. std::function and std::shared_ptr do NOT cross this boundary —
// their layouts are implementation-defined and would break under any stdlib
// mismatch between the caller's DSO and libthorax.

namespace thx::service
{
	// Register a service under an explicit ID + version using a pre-built
	// ServiceFactory. Most callers use the templated overloads below.
	THX_API bool registerService(ServiceID id, Version version, ServiceFactory factory);

	// Register a service under an explicit ID + version, building the factory
	// from any callable. The callable must return IService* (or a convertible
	// raw pointer to a service derived from IService).
	template <typename Callable, typename = std::enable_if_t<
									 !std::is_same_v<std::decay_t<Callable>, ServiceFactory>>>
	inline bool registerService(ServiceID id, Version version, Callable&& callable)
	{
		return registerService(
			std::move(id), version,
			makeServiceFactory(std::forward<Callable>(callable)));
	}

	// Type-deduced registration. T must derive from Service<T> (provides
	// staticId() / staticVersion()) and be default-constructible.
	template <typename T>
	inline bool registerService()
	{
		return registerService(
			T::staticId(), T::staticVersion(),
			ServiceFactory{
				+[](void*) -> IService*
				{ return new T(); },
				+[](void*) noexcept {},
				nullptr,
			});
	}

	// Type-deduced registration with a custom factory callable.
	template <typename T, typename Callable, typename = std::enable_if_t<!std::is_same_v<std::decay_t<Callable>, ServiceFactory>>>
	inline bool registerService(Callable&& callable)
	{
		return registerService(
			T::staticId(), T::staticVersion(),
			makeServiceFactory(std::forward<Callable>(callable)));
	}

	// Type-deduced registration with a pre-built ServiceFactory.
	template <typename T>
	inline bool registerService(ServiceFactory factory)
	{
		return registerService(T::staticId(), T::staticVersion(), factory);
	}

	THX_API bool unregisterService(ServiceID id);

	template <typename T>
	inline bool unregisterService()
	{
		return unregisterService(T::staticId());
	}

	// Returns an already-retained IService* (refcount incremented). Caller
	// must wrap in ServiceHandle::adopt() to take ownership.
	THX_API IService* acquireService(ServiceID id);

	template <typename T>
	inline ServiceHandle<T> getService(ServiceID id)
	{
		static_assert(std::is_base_of_v<IService, T>,
					  "getService<T>: T must derive from thx::service::IService");
		// detail returns an already-retained pointer; adopt without re-retaining.
		// The ServiceID is the type discriminator at lookup time; the cast is
		// just a pointer adjustment — see service_manager.inl for why we don't
		// dynamic_cast across DSOs.
		IService* base = acquireService(std::move(id));
		return base ? ServiceHandle<T>::adopt(static_cast<T*>(base)) : ServiceHandle<T>{};
	}

	template <typename T>
	inline ServiceHandle<T> getService()
	{
		return getService<T>(T::staticId());
	}

	// Snapshot entry returned by listServices().
	struct ServiceInfo
	{
		ServiceID id;
		Version version;
	};
	THX_API std::vector<ServiceInfo> listServices();
} // namespace thx::service
