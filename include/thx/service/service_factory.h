/*
 *  Created by LuckyNeko on 22/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "thx/service/iservice.h"
#include "thx/service/service_handle.h"
#include "thx/thx_api.h"
#include "thx/version_type.h"

#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace thx::service
{

	// Service factory. Wire ABI between the caller's DSO and libthorax.
	//
	// `invoke` produces a raw IService* — ServiceManager wraps it in a
	// ServiceHandle (the first wrap brings the refcount from 0 to 1).
	// `destroyCtx` is called exactly once: after invoke() returns (success or
	// not), or before invoke() if registerService rejects the ID upfront. It
	// must be noexcept since it runs on the cleanup path.
	struct ServiceFactory
	{
		IService* (*invoke)(void* ctx) = nullptr;
		void (*destroyCtx)(void* ctx) noexcept = nullptr;
		void* ctx = nullptr;
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
			+[](void* ctx) -> IService*
			{
				return (*static_cast<Stored*>(ctx))();
			},
			+[](void* ctx) noexcept
			{
				delete static_cast<Stored*>(ctx);
			},
			state,
		};
	}

	// ABI lock-down. ServiceHandle and ServiceFactory cross the DSO boundary;
	// confirm their layout is what consumers expect.
	static_assert(sizeof(ServiceFactory) == 3 * sizeof(void*),
				  "ServiceFactory layout must be { invoke_fn, destroy_fn, ctx }");
} // namespace thx::service
