/*
 *  Created by LuckyNeko on 24/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "thx/plugin_loader.h"
#include "thx/platform.h"

#include <filesystem>

namespace thx
{

PluginLoader::PluginLoader(ServiceManager& sm) : sm_(sm) {}

PluginLoader::~PluginLoader()
{
	for (auto& [path, entry] : plugins_)
	{
		if (entry.plugin)
			entry.plugin->onUnload(sm_);
		else if (entry.legacy_service_id)
			sm_.unregister_service(*entry.legacy_service_id);
	}
	plugins_.clear();
}

std::string PluginLoader::resolve_canonical(std::string const& path)
{
	std::error_code ec;
	auto c = std::filesystem::canonical(path, ec);
	return ec ? std::string{} : c.string();
}

namespace
{
	bool service_registered(ServiceManager const& sm, ServiceID id)
	{
		auto services = sm.list_services();
		for (auto const& s : services)
			if (s.id == id)
				return true;
		return false;
	}

	Result<void, Error> load_iplugin(ServiceManager& sm,
	                                 PluginHandle&   handle,
	                                 std::string const&             canonical,
	                                 std::shared_ptr<IPlugin>&      out_plugin)
	{
		auto* destroy = handle.plugin_destroy_fn();
		IPlugin* raw  = handle.plugin_create_fn()();
		if (!raw)
			return Result<void, Error>::err({ErrorCode::FactoryFailed,
				"thx_create_plugin returned null for: " + canonical});

		// Wrap in shared_ptr now so the deleter runs even on error returns below.
		std::shared_ptr<IPlugin> plugin(raw, [destroy](IPlugin* p)
		{
			if (p && destroy)
				destroy(p);
		});

		// Reject if any required() service is missing.
		auto reqs = plugin->required();
		for (std::size_t i = 0; i < reqs.size(); ++i)
		{
			if (!service_registered(sm, reqs[i]))
			{
				return Result<void, Error>::err({ErrorCode::NotLoaded,
					std::string("plugin requires service '") + reqs[i].name()
					+ "' which is not registered"});
			}
		}

		if (!plugin->onLoad(sm))
		{
			return Result<void, Error>::err({ErrorCode::RegistrationFailed,
				"IPlugin::onLoad returned false for: " + canonical});
		}

		out_plugin = std::move(plugin);
		return Result<void, Error>::ok();
	}

	Result<ServiceID, Error> load_legacy(ServiceManager& sm,
	                                     PluginHandle&   handle,
	                                     std::string const& canonical)
	{
		// Probe: instantiate once to read the service ID and version, then discard.
		// The real instance is created by the factory below when ServiceManager
		// calls it on first registration.
		IService* probe = handle.create_fn()();
		if (!probe)
			return Result<ServiceID, Error>::err({ErrorCode::FactoryFailed,
				"thx_create returned null for: " + canonical});

		ServiceID svc_id  = probe->id();
		Version   svc_ver = probe->version();
		handle.destroy_fn()(probe);

		// Capture function pointers by value; they remain valid while the DSO is
		// open (handle lives in plugins_ after successful registration).
		ServiceCreateFn  create  = handle.create_fn();
		ServiceDestroyFn destroy = handle.destroy_fn();

		bool registered = sm.register_service(svc_id, svc_ver,
			[create, destroy]() -> std::shared_ptr<IService>
			{
				auto* raw = create();
				return make_service(raw, destroy);
			});

		if (!registered)
			return Result<ServiceID, Error>::err({ErrorCode::RegistrationFailed,
				"ServiceManager rejected service: " + std::string(svc_id.name())});

		return Result<ServiceID, Error>::ok(svc_id);
	}
} // namespace

Result<void, Error> PluginLoader::load(std::string const& path)
{
	auto canonical = resolve_canonical(path);
	if (canonical.empty())
		return Result<void, Error>::err({ErrorCode::FileNotFound,
			"Cannot resolve path: " + path});

	if (plugins_.count(canonical))
		return Result<void, Error>::ok(); // already loaded — no-op

	auto open_result = PluginHandle::open(canonical);
	if (!open_result)
		return Result<void, Error>::err(open_result.error());

	PluginHandle handle = std::move(open_result.value());

	if (handle.has_iplugin_abi())
	{
		std::shared_ptr<IPlugin> plugin;
		auto r = load_iplugin(sm_, handle, canonical, plugin);
		if (!r)
			return r;

		Entry entry;
		entry.handle = std::move(handle);
		entry.plugin = std::move(plugin);
		plugins_.emplace(canonical, std::move(entry));
		return Result<void, Error>::ok();
	}

	auto r = load_legacy(sm_, handle, canonical);
	if (!r)
		return Result<void, Error>::err(r.error());

	Entry entry;
	entry.handle            = std::move(handle);
	entry.legacy_service_id = r.value();
	plugins_.emplace(canonical, std::move(entry));
	return Result<void, Error>::ok();
}

Result<void, Error> PluginLoader::unload(std::string const& path)
{
	// Try canonical resolution first; fall back to the raw path as the map key
	// if the file has been deleted since it was loaded.
	auto canonical = resolve_canonical(path);
	if (canonical.empty())
		canonical = path;

	auto it = plugins_.find(canonical);
	if (it == plugins_.end())
		return Result<void, Error>::err({ErrorCode::NotLoaded,
			"Plugin not loaded: " + path});

	if (it->second.plugin)
		it->second.plugin->onUnload(sm_);
	else if (it->second.legacy_service_id)
		sm_.unregister_service(*it->second.legacy_service_id);

	plugins_.erase(it); // ~Entry: plugin destroyed first, then handle dlclose
	return Result<void, Error>::ok();
}

bool PluginLoader::is_loaded(std::string const& path) const
{
	auto canonical = resolve_canonical(path);
	if (!canonical.empty())
		return plugins_.count(canonical) > 0;
	return plugins_.count(path) > 0;
}

std::vector<std::string> PluginLoader::discover(std::string const& directory) const
{
	std::vector<std::string> results;
	std::error_code ec;
	for (auto const& entry : std::filesystem::directory_iterator(directory, ec))
	{
		if (entry.path().extension().string() == kPluginExtension)
			results.push_back(entry.path().string());
	}
	return results;
}

Result<void, Error> PluginLoader::discover_and_load(std::string const& directory)
{
	auto paths = discover(directory);

	Result<void, Error> last_err = Result<void, Error>::ok();
	int loaded = 0;

	for (auto const& p : paths)
	{
		auto r = load(p);
		if (r)
		{
			++loaded;
		}
		else
		{
			thx::log(LogLevel::Warn,
			    "discover_and_load: failed to load '" + p + "': " + r.error().message);
			last_err = std::move(r);
		}
	}

	if (loaded == 0 && !paths.empty())
		return last_err;
	return Result<void, Error>::ok();
}

std::vector<LoadedPluginInfo> PluginLoader::list_plugins() const
{
	std::vector<LoadedPluginInfo> result;
	result.reserve(plugins_.size());
	for (auto const& [path, entry] : plugins_)
	{
		// Legacy plugins report their single service ID. IPlugin-based plugins
		// can register many services and don't have one canonical ID — they
		// appear with an empty ServiceID. (Snapshot redesign comes in a later
		// roadmap milestone.)
		ServiceID id = entry.legacy_service_id
		                   ? *entry.legacy_service_id
		                   : ServiceID("");
		result.push_back({path, id});
	}
	return result;
}

} // namespace thx
