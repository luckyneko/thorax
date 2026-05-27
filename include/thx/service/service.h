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
	// ABI-stable smart pointer over an intrusively-refcounted IService.
	//
	// Layout: a single T* — `sizeof(ServiceHandle<T>) == sizeof(void*)` on
	// every supported platform (statically asserted at the bottom of this
	// header). The refcount lives in IService::m_thxRefcount, so copy / move
	// / destroy don't allocate or read any stdlib-internal control block.
	//
	// Strong reference only. ServiceManager hands out a strong reference and
	// retains its own; callers can keep their handles past unregistration
	// (the registry's onDestroy contract documents this) and the service
	// stays alive until every handle is dropped.
	template <typename T>
	class ServiceHandle
	{
		template <typename> friend class ServiceHandle;

		struct AdoptTag {};
		constexpr ServiceHandle(T* p, AdoptTag) noexcept : m_ptr(p) {}

		T* m_ptr;

	public:
		constexpr ServiceHandle() noexcept                  : m_ptr(nullptr) {}
		constexpr ServiceHandle(std::nullptr_t) noexcept    : m_ptr(nullptr) {}

		// Construct from a raw pointer, incrementing the refcount. The raw
		// pointer must point to a complete object derived from IService.
		explicit ServiceHandle(T* p) noexcept : m_ptr(p)
		{
			if (m_ptr) m_ptr->thxRetain();
		}

		ServiceHandle(ServiceHandle const& other) noexcept : m_ptr(other.m_ptr)
		{
			if (m_ptr) m_ptr->thxRetain();
		}

		ServiceHandle(ServiceHandle&& other) noexcept : m_ptr(other.m_ptr)
		{
			other.m_ptr = nullptr;
		}

		~ServiceHandle()
		{
			if (m_ptr) m_ptr->thxRelease();
		}

		ServiceHandle& operator=(ServiceHandle const& other) noexcept
		{
			if (other.m_ptr) other.m_ptr->thxRetain();
			if (m_ptr) m_ptr->thxRelease();
			m_ptr = other.m_ptr;
			return *this;
		}

		ServiceHandle& operator=(ServiceHandle&& other) noexcept
		{
			if (this != &other)
			{
				if (m_ptr) m_ptr->thxRelease();
				m_ptr = other.m_ptr;
				other.m_ptr = nullptr;
			}
			return *this;
		}

		ServiceHandle& operator=(std::nullptr_t) noexcept
		{
			reset();
			return *this;
		}

		T* get()                const noexcept { return m_ptr;     }
		T& operator*()          const noexcept { return *m_ptr;    }
		T* operator->()         const noexcept { return m_ptr;     }
		explicit operator bool()const noexcept { return m_ptr != nullptr; }

		void reset() noexcept
		{
			if (m_ptr) m_ptr->thxRelease();
			m_ptr = nullptr;
		}

		// Give up ownership without decrementing the refcount. Pairs with
		// adopt() on the receiving side. Used by libthorax's detail layer
		// to hand a retained pointer across the DSO boundary.
		T* detach() noexcept
		{
			T* p = m_ptr;
			m_ptr = nullptr;
			return p;
		}

		// Wrap a raw pointer whose refcount has already been incremented by
		// the caller. Does NOT retain — pairs with detach() on the sending
		// side.
		static ServiceHandle adopt(T* p) noexcept
		{
			return ServiceHandle(p, AdoptTag{});
		}

		friend bool operator==(ServiceHandle const& a, ServiceHandle const& b) noexcept { return a.m_ptr == b.m_ptr; }
		friend bool operator!=(ServiceHandle const& a, ServiceHandle const& b) noexcept { return a.m_ptr != b.m_ptr; }
		friend bool operator==(ServiceHandle const& a, std::nullptr_t)        noexcept  { return a.m_ptr == nullptr; }
		friend bool operator!=(ServiceHandle const& a, std::nullptr_t)        noexcept  { return a.m_ptr != nullptr; }
		friend bool operator==(std::nullptr_t, ServiceHandle const& a)        noexcept  { return a.m_ptr == nullptr; }
		friend bool operator!=(std::nullptr_t, ServiceHandle const& a)        noexcept  { return a.m_ptr != nullptr; }
	};

	// Snapshot entry returned by listServices().
	struct ServiceInfo
	{
		ServiceID id;
		Version   version;
	};

	// Service factory. Wire ABI between the caller's DSO and libthorax.
	//
	// `invoke` produces a raw IService* — ServiceManager wraps it in a
	// ServiceHandle (the first wrap brings the refcount from 0 to 1).
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
		THX_API bool                     registerServiceImpl(ServiceID id, Version version, ServiceFactory factory);
		THX_API bool                     unregisterServiceImpl(ServiceID id);
		// Returns an already-retained IService* (refcount incremented). Caller
		// must wrap in ServiceHandle::adopt() to take ownership.
		THX_API IService*                acquireServiceImpl(ServiceID id);
		THX_API std::vector<ServiceInfo> listServicesImpl();
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
	inline ServiceHandle<T> getService(ServiceID id)
	{
		static_assert(std::is_base_of_v<IService, T>,
		    "getService<T>: T must derive from thx::service::IService");
		// detail returns an already-retained pointer; adopt without re-retaining.
		// The ServiceID is the type discriminator at lookup time; the cast is
		// just a pointer adjustment — see service_manager.inl for why we don't
		// dynamic_cast across DSOs.
		IService* base = detail::acquireServiceImpl(std::move(id));
		return base ? ServiceHandle<T>::adopt(static_cast<T*>(base)) : ServiceHandle<T>{};
	}

	template <typename T>
	inline ServiceHandle<T> getService()
	{
		return getService<T>(T::staticId());
	}

	inline std::vector<ServiceInfo> listServices()
	{
		return detail::listServicesImpl();
	}

	// ABI lock-down. ServiceHandle and ServiceFactory cross the DSO boundary;
	// confirm their layout is what consumers expect.
	static_assert(sizeof(ServiceHandle<IService>) == sizeof(void*),
	    "ServiceHandle must be a single-pointer type for ABI stability");
	static_assert(sizeof(ServiceFactory) == 3 * sizeof(void*),
	    "ServiceFactory layout must be { invoke_fn, destroy_fn, ctx }");

} // namespace thx::service
