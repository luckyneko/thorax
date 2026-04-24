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
#include <iostream>

// TODO(M5): replace std::cerr with thx::log() routed through ILogSink.

namespace thx
{

PluginLoader::PluginLoader(ServiceManager& sm) : sm_(sm) {}

PluginLoader::~PluginLoader()
{
	for (auto& [path, entry] : plugins_)
		sm_.unregister_service(entry.service_id);
	plugins_.clear();
}

std::string PluginLoader::resolve_canonical(std::string const& path)
{
	std::error_code ec;
	auto c = std::filesystem::canonical(path, ec);
	return ec ? std::string{} : c.string();
}

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

	// Probe: instantiate once to read the service ID and version, then discard.
	// The real instance is created by the factory below when ServiceManager
	// calls it on first registration.
	IService* probe = handle.create_fn()();
	if (!probe)
		return Result<void, Error>::err({ErrorCode::FactoryFailed,
			"thx_create returned null for: " + canonical});

	ServiceID svc_id  = probe->id();
	Version   svc_ver = probe->version();
	handle.destroy_fn()(probe);

	// Capture function pointers by value; they remain valid while the DSO is
	// open (handle lives in plugins_ after successful registration).
	ServiceCreateFn  create  = handle.create_fn();
	ServiceDestroyFn destroy = handle.destroy_fn();

	bool registered = sm_.register_service(svc_id, svc_ver,
		[create, destroy]() -> std::shared_ptr<IService>
		{
			auto* raw = create();
			return make_service(raw, destroy);
		});

	if (!registered)
		return Result<void, Error>::err({ErrorCode::RegistrationFailed,
			"ServiceManager rejected service: " + std::string(svc_id.name())});

	plugins_.emplace(canonical, Entry{std::move(handle), svc_id});
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

	sm_.unregister_service(it->second.service_id);
	plugins_.erase(it); // PluginHandle destructor closes the DSO
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
			std::cerr << "[thorax] Failed to load plugin " << p
			          << ": " << r.error().message << '\n';
			last_err = std::move(r);
		}
	}

	if (loaded == 0 && !paths.empty())
		return last_err;
	return Result<void, Error>::ok();
}

} // namespace thx
