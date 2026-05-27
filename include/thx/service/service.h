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
// ServiceFactory is intentionally a plain {fn-ptr, destroy-fn, void*} struct
// rather than std::function: std::function's layout is implementation-defined,
// and it's the wire ABI type between the calling DSO and libthorax. The
// template wrappers below let users pass any callable — they allocate the
// capture state in the caller's TU heap, build the struct in C-compatible
// terms, and let the destroy-fn release the capture on the libthorax side.

namespace thx::service
{
	// Snapshot entry returned by listServices().
	struct ServiceInfo
	{
		ServiceID id;
		Version   version;
	};

	// Service factory. Wire ABI between the caller's DSO and libthorax.
	//
	// `invoke` produces a raw IService* — ServiceManager takes ownership and
	// wraps in shared_ptr<IService> (relying on IService's virtual destructor).
	// `destroyCtx` is called exactly once: after invoke() returns (success or
	// not), or before invoke() if registerService rejects the ID upfront. It
	// must be noexcept since it runs on the cleanup path.
	struct ServiceFactory
	{
		IService* (*invoke)(void* ctx)            = nullptr;
		void      (*destroyCtx)(void* ctx) noexcept = nullptr;
		void*       ctx                           = nullptr;
	};

	// Build a ServiceFactory that adapts an arbitrary callable. The callable
	// must be invocable with no arguments and return something convertible to
	// `IService*`. The captured state lives in a heap allocation owned by the
	// returned factory — released when ServiceManager calls destroyCtx.
	template <typename Callable>
	inline ServiceFactory makeServiceFactory(Callable&& callable)
	{
		using Stored = std::decay_t<Callable>;
		auto* state = new Stored(std::forward<Callable>(callable));
		return ServiceFactory{
			+[](void* ctx) -> IService* {
				return (*static_cast<Stored*>(ctx))();
			},
			+[](void* ctx) noexcept {
				delete static_cast<Stored*>(ctx);
			},
			state,
		};
	}

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

	// Register a service under an explicit ID + version using a pre-built
	// ServiceFactory. Most callers use the templated overloads below.
	inline bool registerService(ServiceID id, Version version, ServiceFactory factory)
	{
		return detail::registerServiceImpl(std::move(id), version, factory);
	}

	// Register a service under an explicit ID + version, building the factory
	// from any callable. The callable must return IService* (or a convertible
	// raw pointer to a service derived from IService).
	template <typename Callable, typename = std::enable_if_t<
		!std::is_same_v<std::decay_t<Callable>, ServiceFactory>>>
	inline bool registerService(ServiceID id, Version version, Callable&& callable)
	{
		return detail::registerServiceImpl(
		    std::move(id), version,
		    makeServiceFactory(std::forward<Callable>(callable)));
	}

	// Type-deduced registration. T must derive from Service<T> (provides
	// staticId() / staticVersion()) and be default-constructible.
	template <typename T>
	inline bool registerService()
	{
		return detail::registerServiceImpl(
		    T::staticId(), T::staticVersion(),
		    ServiceFactory{
		        +[](void*) -> IService* { return new T(); },
		        +[](void*) noexcept {},
		        nullptr,
		    });
	}

	// Type-deduced registration with a custom factory callable.
	template <typename T, typename Callable, typename = std::enable_if_t<
		!std::is_same_v<std::decay_t<Callable>, ServiceFactory>>>
	inline bool registerService(Callable&& callable)
	{
		return detail::registerServiceImpl(
		    T::staticId(), T::staticVersion(),
		    makeServiceFactory(std::forward<Callable>(callable)));
	}

	// Type-deduced registration with a pre-built ServiceFactory.
	template <typename T>
	inline bool registerService(ServiceFactory factory)
	{
		return detail::registerServiceImpl(T::staticId(), T::staticVersion(), factory);
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
