/*
 *  Created by LuckyNeko on 22/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "thx/service_id.h"
#include "thx/version_type.h"

namespace thx
{
	// Base interface for all services registered with the ServiceManager.
	// Concrete services inherit this and add their own API on top.
	class IService
	{
	public:
		virtual ~IService() = default;

		virtual ServiceID id() const = 0;
		virtual Version version() const = 0;

		// Called once by ServiceManager after the service is first constructed
		// and before it becomes visible to callers. Return false to abort
		// registration (the service will be discarded without being inserted).
		// Must not call ServiceManager methods — the registry lock is held.
		virtual bool onConstruct() { return true; }

		// Called once by ServiceManager when the last registrant unregisters
		// the service, before the service object is destroyed.
		// The registry lock is NOT held here, so ServiceManager may be called.
		virtual void onDestroy() {}
	};

} // namespace thx
