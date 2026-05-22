/*
 *  Created by LuckyNeko on 24/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "thx/plugin_manager.h"
#include "thx/plugin_garbage.h"
#include "thx/to_string.h"
#include "thx/platform.h"

#include <algorithm>
#include <filesystem>
#include <unordered_set>

namespace thx
{

PluginManager::PluginManager(ServiceManager& sm) : sm_(sm) {}

// --- OpenedPlugin ----------------------------------------------------------

OpenedPlugin::OpenedPlugin(PluginHandle handle,
                           std::shared_ptr<IPlugin> plugin,
                           std::string canonical)
    : handle_(std::move(handle))
    , plugin_(std::move(plugin))
    , canonical_(std::move(canonical))
{
}

OpenedPlugin::OpenedPlugin(OpenedPlugin&&) noexcept            = default;
OpenedPlugin& OpenedPlugin::operator=(OpenedPlugin&&) noexcept = default;

OpenedPlugin::~OpenedPlugin()
{
	// Reset plugin_ before handle_ goes out of scope so the IPlugin destructor
	// (which lives in DSO code) runs while the DSO is still mapped. ~PluginHandle
	// then defers the dlclose into the graveyard.
	plugin_.reset();
}

StringView OpenedPlugin::name() const noexcept
{
	return plugin_ ? plugin_->name() : StringView{};
}

Version OpenedPlugin::version() const noexcept
{
	return plugin_ ? plugin_->version() : Version{};
}

Span<const ServiceRequirement> OpenedPlugin::required() const noexcept
{
	return plugin_ ? plugin_->required() : Span<const ServiceRequirement>{};
}

namespace
{
	// Force-unregister any service IDs the plugin's onUnload neglected to drop.
	// A well-behaved IPlugin removes everything it registered; this is a safety
	// net against third-party plugins that forget.
	void sweep_surviving_services(ServiceManager&               sm,
	                              std::vector<ServiceID> const& ids,
	                              std::string const&            plugin_name)
	{
		for (auto const& id : ids)
		{
			if (sm.get_service<IService>(id))
			{
				thx::log(LogLevel::Warn,
				    std::string("PluginManager: plugin '") + plugin_name
				    + "' left service '" + id.name()
				    + "' registered after onUnload; force-unregistering");
				sm.unregister_service(id);
			}
		}
	}
} // namespace

PluginManager::~PluginManager()
{
	for (auto& [path, entry] : plugins_)
	{
		std::string name = entry.plugin
		                       ? std::string(static_cast<std::string_view>(entry.plugin->name()))
		                       : path;
		if (entry.plugin)
			entry.plugin->onUnload(sm_);
		sweep_surviving_services(sm_, entry.service_ids, name);
	}
	plugins_.clear();
}

std::string PluginManager::resolve_canonical(std::string const& path)
{
	std::error_code ec;
	auto c = std::filesystem::canonical(path, ec);
	return ec ? std::string{} : c.string();
}

Result<void, Error> PluginManager::check_requirements(ServiceManager const&          sm,
                                                     Span<const ServiceRequirement> reqs)
{
	for (std::size_t i = 0; i < reqs.size(); ++i)
	{
		auto const& req = reqs[i];
		auto svc = sm.get_service<IService>(req.id);
		if (!svc)
			return Result<void, Error>::err({ErrorCode::NotLoaded,
				std::string("plugin requires service '") + req.id.name()
				+ "' which is not registered"});

		if (!Version::compatible(req.version, svc->version()))
		{
			std::string msg = std::string("plugin requires service '") + req.id.name() + "' at "
			    + to_string(req.version)
			    + "; registered version is "
			    + to_string(svc->version());
			return Result<void, Error>::err({ErrorCode::VersionMismatch, std::move(msg)});
		}
	}
	return Result<void, Error>::ok();
}

Result<OpenedPlugin, Error> PluginManager::open(std::string const& path)
{
	// Drain the deferred-close queue before any new dlopen so we don't
	// accumulate a long tail of mapped-but-released DSOs in long-running
	// processes. Safe at this point: any references that survived the previous
	// unload have either been released by now (the user's responsibility) or
	// the user is intentionally keeping them alive — in which case they should
	// not be calling open() yet.
	PluginGarbage::instance().collect();

	auto canonical = resolve_canonical(path);
	if (canonical.empty())
		return Result<OpenedPlugin, Error>::err({ErrorCode::FileNotFound,
			"Cannot resolve path: " + path});

	if (plugins_.count(canonical))
		return Result<OpenedPlugin, Error>::err({ErrorCode::AlreadyLoaded,
			"Plugin already loaded: " + canonical});

	auto open_result = PluginHandle::open(canonical);
	if (!open_result)
		return Result<OpenedPlugin, Error>::err(open_result.error());

	PluginHandle handle = std::move(open_result.value());

	auto* destroy = handle.destroy_fn();
	IPlugin* raw  = handle.create_fn()();
	if (!raw)
		return Result<OpenedPlugin, Error>::err({ErrorCode::FactoryFailed,
			"thx_create_plugin returned null for: " + canonical});

	// Wrap in shared_ptr so destruction routes back through the DSO's destroy
	// function. The destroy function pointer remains valid while the handle is
	// alive — OpenedPlugin's destruction order (plugin_ before handle_)
	// guarantees that.
	std::shared_ptr<IPlugin> plugin(raw, [destroy](IPlugin* p)
	{
		if (p && destroy)
			destroy(p);
	});

	return Result<OpenedPlugin, Error>::ok(
	    OpenedPlugin(std::move(handle), std::move(plugin), std::move(canonical)));
}

Result<void, Error> PluginManager::load(OpenedPlugin opened)
{
	if (!opened)
		return Result<void, Error>::err({ErrorCode::Unknown,
			"PluginManager::load called with empty OpenedPlugin"});

	// Guard against a race / programming error: another entry with the same
	// canonical path appearing between open() and load().
	if (plugins_.count(opened.canonical_))
		return Result<void, Error>::err({ErrorCode::AlreadyLoaded,
			"Plugin already loaded: " + opened.canonical_});

	if (auto r = check_requirements(sm_, opened.plugin_->required()); !r)
		return r;

	// Snapshot the registry so we can attribute new registrations to this plugin.
	auto before = sm_.list_services();
	std::unordered_set<ServiceID> before_ids;
	before_ids.reserve(before.size());
	for (auto const& s : before)
		before_ids.insert(s.id);

	auto diff_new_ids = [&]() -> std::vector<ServiceID>
	{
		std::vector<ServiceID> ids;
		for (auto const& s : sm_.list_services())
			if (!before_ids.count(s.id))
				ids.push_back(s.id);
		std::sort(ids.begin(), ids.end(),
		    [](ServiceID const& a, ServiceID const& b)
		    {
		        return std::string_view(a.name()) < std::string_view(b.name());
		    });
		return ids;
	};

	if (!opened.plugin_->onLoad(sm_))
	{
		// onLoad may have partially registered services before returning false.
		// Unregister them so the failed load leaves the registry as it was.
		for (auto const& id : diff_new_ids())
			sm_.unregister_service(id);
		return Result<void, Error>::err({ErrorCode::RegistrationFailed,
			"IPlugin::onLoad returned false for: " + opened.canonical_});
	}

	Entry entry;
	entry.handle      = std::move(opened.handle_);
	entry.service_ids = diff_new_ids();
	entry.plugin      = std::move(opened.plugin_);
	plugins_.emplace(opened.canonical_, std::move(entry));
	return Result<void, Error>::ok();
}

Result<void, Error> PluginManager::load(std::string const& path)
{
	// Preserve the historical "loading the same path twice is a no-op"
	// behaviour. open() reports AlreadyLoaded as an error; here we swallow it.
	auto canonical = resolve_canonical(path);
	if (!canonical.empty() && plugins_.count(canonical))
		return Result<void, Error>::ok();

	auto opened = open(path);
	if (!opened)
		return Result<void, Error>::err(std::move(opened.error()));

	return load(std::move(opened.value()));
}

Result<void, Error> PluginManager::unload(std::string const& path)
{
	// Map keys are always canonical paths from a successful load(); if the
	// caller's path can't be canonicalized now (file deleted, or never existed)
	// we can't find the entry. Report NotLoaded — from the caller's point of
	// view "no such file" and "loaded under a different path" are both "this
	// thing isn't loaded right now."
	auto canonical = resolve_canonical(path);
	if (canonical.empty())
		return Result<void, Error>::err({ErrorCode::NotLoaded,
			"Plugin not loaded (path cannot be resolved): " + path});

	auto it = plugins_.find(canonical);
	if (it == plugins_.end())
		return Result<void, Error>::err({ErrorCode::NotLoaded,
			"Plugin not loaded: " + path});

	std::string name = it->second.plugin
	                       ? std::string(static_cast<std::string_view>(it->second.plugin->name()))
	                       : canonical;
	if (it->second.plugin)
		it->second.plugin->onUnload(sm_);
	sweep_surviving_services(sm_, it->second.service_ids, name);

	plugins_.erase(it); // ~Entry: plugin destroyed first, then handle dlclose
	return Result<void, Error>::ok();
}

bool PluginManager::is_loaded(std::string const& path) const
{
	// Map keys are canonical paths; if canonicalization fails the file isn't
	// reachable on disk and therefore can't match any loaded entry.
	auto canonical = resolve_canonical(path);
	if (canonical.empty())
		return false;
	return plugins_.count(canonical) > 0;
}

std::vector<std::string> PluginManager::discover(std::string const& directory) const
{
	std::vector<std::string> results;
	std::error_code ec;
	for (auto const& entry : std::filesystem::directory_iterator(directory, ec))
	{
		if (entry.path().extension().string() == kPluginExtension)
			results.push_back(entry.path().string());
	}
	// Filesystem iteration order is unspecified; sort so load order is
	// reproducible across runs and platforms.
	std::sort(results.begin(), results.end());
	return results;
}

PluginManager::LoadSummary PluginManager::discover_and_load(std::string const& directory)
{
	LoadSummary summary;
	for (auto const& p : discover(directory))
	{
		auto r = load(p);
		if (r)
		{
			summary.loaded.push_back(p);
		}
		else
		{
			thx::log(LogLevel::Warn,
			    "discover_and_load: failed to load '" + p + "': " + r.error().message);
			summary.failed.emplace_back(p, std::move(r.error()));
		}
	}
	return summary;
}

std::vector<LoadedPluginInfo> PluginManager::list_plugins() const
{
	std::vector<LoadedPluginInfo> result;
	result.reserve(plugins_.size());
	for (auto const& [path, entry] : plugins_)
	{
		std::string name = entry.plugin
		                       ? std::string(static_cast<std::string_view>(entry.plugin->name()))
		                       : std::string{};
		result.push_back({path, std::move(name), entry.service_ids});
	}
	return result;
}

} // namespace thx
