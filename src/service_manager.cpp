/*
 *  Created by LuckyNeko on 22/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "thx/service_manager.h"

#include <iostream>

// TODO(M5): replace std::cerr calls with thx::log() routed through ILogSink.

namespace thx
{

	ServiceManager& ServiceManager::instance()
	{
		static ServiceManager inst;
		return inst;
	}

	bool ServiceManager::register_service(ServiceID id, Version version, ServiceFactory factory)
	{
		if (!factory)
		{
			std::cerr << "[thorax] register_service: null factory for '"
					  << id.name() << "'\n";
			return false;
		}

		std::unique_lock lock(mutex_);

		auto it = services_.find(id);
		if (it != services_.end())
		{
			// Already registered — accept only if compatible (same major, new >= existing).
			if (!compatible(it->second.service->version(), version))
			{
				auto const& ev = it->second.service->version();
				std::cerr << "[thorax] register_service: incompatible version for '"
						  << id.name() << "'"
						  << " (registered=" << ev.major << "." << ev.minor << "." << ev.patch
						  << ", requested=" << version.major << "." << version.minor
						  << "." << version.patch << ")\n";
				return false;
			}

			++it->second.ref_count;
			return true;
		}

		// First registration — construct the service.
		auto service = factory();
		if (!service)
		{
			std::cerr << "[thorax] register_service: factory returned null for '"
					  << id.name() << "'\n";
			return false;
		}

		if (!service->onConstruct())
		{
			std::cerr << "[thorax] register_service: onConstruct failed for '"
					  << id.name() << "'\n";
			return false;
		}

		services_.emplace(id, Entry{std::move(service), 1});
		return true;
	}

	bool ServiceManager::unregister_service(ServiceID id)
	{
		std::shared_ptr<IService> to_destroy;

		{
			std::unique_lock lock(mutex_);

			auto it = services_.find(id);
			if (it == services_.end())
			{
				std::cerr << "[thorax] unregister_service: '"
						  << id.name() << "' is not registered\n";
				return false;
			}

			if (--it->second.ref_count == 0)
			{
				to_destroy = std::move(it->second.service);
				services_.erase(it);
			}
		}

		// Call onDestroy outside the lock so the service may safely call
		// ServiceManager methods during shutdown.
		if (to_destroy)
		{
			to_destroy->onDestroy();
			return true;
		}

		return false;
	}

} // namespace thx
