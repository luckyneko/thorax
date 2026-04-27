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
#include <unordered_set>

namespace thx
{

namespace detail
{
	// Defined in plugin_handle.cpp alongside the graveyard storage.
	std::size_t drain_dso_graveyard() noexcept;
	std::size_t pending_dso_graveyard() noexcept;
} // namespace detail

std::size_t collect_plugin_garbage() noexcept
{
	return detail::drain_dso_graveyard();
}

std::size_t pending_plugin_garbage() noexcept
{
	return detail::pending_dso_graveyard();
}


PluginLoader::PluginLoader(ServiceManager& sm) : sm_(sm) {}

PluginLoader::~PluginLoader()
{
	for (auto& [path, entry] : plugins_)
	{
		if (entry.plugin)
			entry.plugin->onUnload(sm_);
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
	                                 std::shared_ptr<IPlugin>&      out_plugin,
	                                 std::vector<ServiceID>&        out_new_ids)
	{
		auto* destroy = handle.destroy_fn();
		IPlugin* raw  = handle.create_fn()();
		if (!raw)
			return Result<void, Error>::err({ErrorCode::FactoryFailed,
				"thx_create_plugin returned null for: " + canonical});

		// Wrap in shared_ptr now so the deleter runs even on error returns below.
		// The destroy function pointer remains valid while `handle` is alive,
		// which is guaranteed to outlive the IPlugin (Entry destruction order).
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

		// Snapshot the registry so we can attribute new registrations to this plugin.
		auto before = sm.list_services();
		std::unordered_set<ServiceID> before_ids;
		before_ids.reserve(before.size());
		for (auto const& s : before)
			before_ids.insert(s.id);

		if (!plugin->onLoad(sm))
		{
			return Result<void, Error>::err({ErrorCode::RegistrationFailed,
				"IPlugin::onLoad returned false for: " + canonical});
		}

		auto after = sm.list_services();
		for (auto const& s : after)
			if (!before_ids.count(s.id))
				out_new_ids.push_back(s.id);

		out_plugin = std::move(plugin);
		return Result<void, Error>::ok();
	}
} // namespace

Result<void, Error> PluginLoader::load(std::string const& path)
{
	// Drain the deferred-close queue before any new dlopen so we don't
	// accumulate a long tail of mapped-but-released DSOs in long-running
	// processes. Safe at this point: any references that survived the previous
	// unload have either been released by now (the user's responsibility) or
	// the user is intentionally keeping them alive — in which case they should
	// not be calling load() yet.
	detail::drain_dso_graveyard();

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

	std::shared_ptr<IPlugin> plugin;
	std::vector<ServiceID>   new_ids;
	auto r = load_iplugin(sm_, handle, canonical, plugin, new_ids);
	if (!r)
		return r;

	Entry entry;
	entry.handle      = std::move(handle);
	entry.service_ids = std::move(new_ids);
	entry.plugin      = std::move(plugin);
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
		// Plugins that registered ≥1 service report the first as their primary;
		// plugins that registered none appear with a sentinel empty ServiceID.
		// A richer per-plugin service list is part of a later roadmap milestone.
		ServiceID id = entry.service_ids.empty()
		                   ? ServiceID("")
		                   : entry.service_ids.front();
		result.push_back({path, id});
	}
	return result;
}

} // namespace thx
