/*
 *  Created by LuckyNeko on 22/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "thx/service/service_id.h"
#include "thx/thx_api.h"
#include "thx/version_type.h"

#include <atomic>
#include <cstdint>

namespace thx::service
{
	template <typename T> class ServiceHandle;

	// Base interface for all services registered with the ServiceManager.
	// Concrete services inherit this and add their own API on top.
	//
	// THX_API at class scope exports IService's typeinfo and vtable. Required
	// for the typeinfo to live at one address across the plugin-DSO boundary.
	//
	// Lifetime: managed by an intrusive atomic refcount (`m_thxRefcount`) and
	// the ServiceHandle<T> smart pointer. ServiceHandle is the public means of
	// holding a strong reference; the refcount field exists so we can avoid
	// std::shared_ptr's implementation-defined control block crossing DSO
	// boundaries. Atomic ops on `std::atomic<uint32_t>` for a trivial type
	// compile to plain hardware instructions and have a stable ABI across
	// every compiler we support.
	class THX_API IService
	{
	public:
		// Out-of-line in src/abi.cpp so libthorax owns IService's vtable and
		// typeinfo. See abi.cpp for the cross-DSO rationale.
		virtual ~IService();

		virtual ServiceID id() const = 0;
		virtual Version version() const = 0;

		// Called once by ServiceManager after the service is first constructed
		// and before it becomes visible to callers. Return false to abort
		// registration (the service will be discarded without being inserted).
		// The registry lock is NOT held here, so calling back into the
		// ServiceManager is safe — except that a recursive registerService for
		// the same ID will see the in-flight reservation and bail out.
		virtual bool onConstruct() { return true; }

		// Called by ServiceManager when the service is unregistered, after the
		// entry has been removed from the map but while at least one strong
		// ServiceHandle to the service is still alive (the registry's own
		// handle). The underlying object is destroyed when the last handle
		// drops, which may be later than onDestroy() if any external caller is
		// still holding one. Treat onDestroy() as "the service is leaving the
		// registry" — not "the service is about to be deleted."
		//
		// The registry lock is NOT held here, so ServiceManager may be called.
		virtual void onDestroy() {}

	private:
		template <typename> friend class ServiceHandle;

		// Intrusive strong refcount. Initial value is 0; the first
		// ServiceHandle wrapping a freshly-allocated IService brings it to 1.
		// std::atomic<uint32_t> for a trivial T is layout-equivalent to a
		// plain uint32_t — the atomic ops emit hardware instructions, not
		// stdlib-internal data structures, so this is ABI-stable across DSOs.
		mutable std::atomic<std::uint32_t> m_thxRefcount{0};

		// Inline so the refcount mutation happens in the caller's TU. Relaxed
		// retain (we already hold a reference); acquire/release on the final
		// decrement so observers see all prior writes.
		void thxRetain() const noexcept
		{
			m_thxRefcount.fetch_add(1, std::memory_order_relaxed);
		}
		void thxRelease() const noexcept
		{
			if (m_thxRefcount.fetch_sub(1, std::memory_order_acq_rel) == 1)
				delete this;
		}
	};

	// CRTP base that wires the IService virtual interface to static metadata on
	// the derived type. Derived must provide:
	//
	//   static constexpr thx::Version staticVersion();
	//
	// staticId() has a default implementation that derives the ID from the
	// C++ qualified name (e.g. thx::io::FileService -> "thx.io.FileService").
	// Derived may override it with an explicit ID if a custom name is preferred.
	//
	// Example:
	//
	//   class ImageLoadingService : public thx::service::Service<ImageLoadingService>
	//   {
	//   public:
	//       static constexpr thx::Version staticVersion()
	//           { return thx::Version{1, 0, 0}; }
	//
	//       // Service-specific API:
	//       virtual void addLoader(std::shared_ptr<IImageLoader>) = 0;
	//   };
	//
	// Plugin B includes this interface header and calls:
	//   sm.getService<ImageLoadingService>()
	// with no knowledge of Plugin A's implementation.
	template <typename Derived>
	class Service : public IService
	{
	public:
		// Default: converts the C++ qualified name to dot-notation.
		// Override in Derived if a different canonical name is needed.
		static constexpr ServiceID staticId() noexcept
		{
			return ServiceID::from<Derived>();
		}

		// Must be provided by Derived, e.g.:
		//   static constexpr thx::Version staticVersion()
		//       { return thx::Version{1, 0, 0}; }

		// final prevents downstream types from changing the identity of an
		// already-defined service interface.
		ServiceID id() const final { return Derived::staticId(); }
		Version version() const final { return Derived::staticVersion(); }
	};

} // namespace thx::service
