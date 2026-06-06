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
		template <typename>
		friend class ServiceHandle;

		struct AdoptTag
		{
		};
		constexpr ServiceHandle(T* p, AdoptTag) noexcept
			: m_ptr(p)
		{
		}

		T* m_ptr;

	public:
		constexpr ServiceHandle() noexcept
			: m_ptr(nullptr)
		{
		}
		constexpr ServiceHandle(std::nullptr_t) noexcept
			: m_ptr(nullptr)
		{
		}

		// Construct from a raw pointer, incrementing the refcount. The raw
		// pointer must point to a complete object derived from IService.
		explicit ServiceHandle(T* p) noexcept
			: m_ptr(p)
		{
			if (m_ptr)
				m_ptr->thxRetain();
		}

		ServiceHandle(ServiceHandle const& other) noexcept
			: m_ptr(other.m_ptr)
		{
			if (m_ptr)
				m_ptr->thxRetain();
		}

		ServiceHandle(ServiceHandle&& other) noexcept
			: m_ptr(other.m_ptr)
		{
			other.m_ptr = nullptr;
		}

		~ServiceHandle()
		{
			if (m_ptr)
				m_ptr->thxRelease();
		}

		ServiceHandle& operator=(ServiceHandle const& other) noexcept
		{
			if (other.m_ptr)
				other.m_ptr->thxRetain();
			if (m_ptr)
				m_ptr->thxRelease();
			m_ptr = other.m_ptr;
			return *this;
		}

		ServiceHandle& operator=(ServiceHandle&& other) noexcept
		{
			if (this != &other)
			{
				if (m_ptr)
					m_ptr->thxRelease();
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

		T* get() const noexcept { return m_ptr; }
		T& operator*() const noexcept { return *m_ptr; }
		T* operator->() const noexcept { return m_ptr; }
		explicit operator bool() const noexcept { return m_ptr != nullptr; }

		void reset() noexcept
		{
			if (m_ptr)
				m_ptr->thxRelease();
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
		friend bool operator==(ServiceHandle const& a, std::nullptr_t) noexcept { return a.m_ptr == nullptr; }
		friend bool operator!=(ServiceHandle const& a, std::nullptr_t) noexcept { return a.m_ptr != nullptr; }
		friend bool operator==(std::nullptr_t, ServiceHandle const& a) noexcept { return a.m_ptr == nullptr; }
		friend bool operator!=(std::nullptr_t, ServiceHandle const& a) noexcept { return a.m_ptr != nullptr; }
	};

	// ABI lock-down. ServiceHandle and ServiceFactory cross the DSO boundary;
	// confirm their layout is what consumers expect.
	static_assert(sizeof(ServiceHandle<IService>) == sizeof(void*),
				  "ServiceHandle must be a single-pointer type for ABI stability");
} // namespace thx::service
