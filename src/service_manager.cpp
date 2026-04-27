/*
 *  Created by LuckyNeko on 22/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "thx/service_manager.h"

#include <sstream>

namespace thx
{

ServiceManager& ServiceManager::instance()
{
	static ServiceManager inst;
	return inst;
}

void ServiceManager::set_log_sink(std::shared_ptr<ILogSink> sink)
{
	thx::set_log_sink(std::move(sink));
}

bool ServiceManager::register_service(ServiceID id, Version version, ServiceFactory factory)
{
	if (!factory)
	{
		thx::log(LogLevel::Error,
		    std::string("register_service: null factory for '") + id.name() + "'");
		return false;
	}

	std::unique_lock lock(mutex_);

	if (services_.find(id) != services_.end())
	{
		thx::log(LogLevel::Warn,
		    std::string("register_service: '") + id.name()
		    + "' is already registered (single-owner registry)");
		return false;
	}

	auto service = factory();
	if (!service)
	{
		thx::log(LogLevel::Error,
		    std::string("register_service: factory returned null for '") + id.name() + "'");
		return false;
	}

	// Verify the service reports the version the caller claimed; mismatch is
	// a programming error worth flagging early.
	if (service->version() != version)
	{
		std::ostringstream msg;
		auto const& ev = service->version();
		msg << "register_service: declared version " << version.major << '.' << version.minor
		    << '.' << version.patch
		    << " does not match service-reported " << ev.major << '.' << ev.minor << '.' << ev.patch
		    << " for '" << id.name() << "'";
		thx::log(LogLevel::Warn, msg.str());
	}

	if (!service->onConstruct())
	{
		thx::log(LogLevel::Error,
		    std::string("register_service: onConstruct failed for '") + id.name() + "'");
		return false;
	}

	services_.emplace(id, Entry{std::move(service)});
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
			thx::log(LogLevel::Warn,
			    std::string("unregister_service: '") + id.name() + "' is not registered");
			return false;
		}

		to_destroy = std::move(it->second.service);
		services_.erase(it);
	}

	// onDestroy runs without the registry lock so the service may safely call
	// ServiceManager methods during shutdown.
	if (to_destroy)
		to_destroy->onDestroy();
	return true;
}

std::vector<ServiceInfo> ServiceManager::list_services() const
{
	std::shared_lock lock(mutex_);
	std::vector<ServiceInfo> result;
	result.reserve(services_.size());
	for (auto const& [id, entry] : services_)
		result.push_back({id});
	return result;
}

} // namespace thx
