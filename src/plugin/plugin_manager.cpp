/*
 *  Created by LuckyNeko on 24/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "thx/plugin/plugin_manager.h"
#include "thx/library.h"
#include "thx/registry.h"
#include "thx/to_string.h"
#include "thx/plugin/platform.h"

#include <algorithm>
#include <filesystem>
#include <unordered_set>

namespace thx::plugin
{

// Bring the service-layer types we touch heavily into scope, so the
// implementation reads the same as before the namespace split. The public
// header still uses fully-qualified names.
using thx::service::IService;
using thx::service::ServiceID;
using thx::service::ServiceManager;

PluginManager::PluginManager(ServiceManager& sm) : m_sm(sm) {}

// --- OpenedPlugin ----------------------------------------------------------

OpenedPlugin::OpenedPlugin(PluginHandle handle,
                           std::shared_ptr<IPlugin> plugin,
                           std::string canonical)
    : m_handle(std::move(handle))
    , m_plugin(std::move(plugin))
    , m_canonical(std::move(canonical))
{
}

OpenedPlugin::OpenedPlugin(OpenedPlugin&&) noexcept            = default;
OpenedPlugin& OpenedPlugin::operator=(OpenedPlugin&&) noexcept = default;

OpenedPlugin::~OpenedPlugin()
{
	// Reset m_plugin before m_handle goes out of scope so the IPlugin destructor
	// (which lives in DSO code) runs while the DSO is still mapped. ~PluginHandle
	// then defers the dlclose into the graveyard.
	m_plugin.reset();
}

StringView OpenedPlugin::name() const noexcept
{
	return m_plugin ? m_plugin->name() : StringView{};
}

Version OpenedPlugin::version() const noexcept
{
	return m_plugin ? m_plugin->version() : Version{};
}

Span<const ServiceRequirement> OpenedPlugin::required() const noexcept
{
	return m_plugin ? m_plugin->required() : Span<const ServiceRequirement>{};
}

namespace
{
	// Force-unregister any service IDs the plugin's onUnload neglected to drop.
	// A well-behaved IPlugin removes everything it registered; this is a safety
	// net against third-party plugins that forget.
	void sweepSurvivingServices(ServiceManager&               sm,
	                              std::vector<ServiceID> const& ids,
	                              std::string const&            pluginName)
	{
		for (auto const& id : ids)
		{
			if (sm.getService<IService>(id))
			{
				thx::log(LogLevel::Warn,
				    std::string("PluginManager: plugin '") + pluginName
				    + "' left service '" + id.name()
				    + "' registered after onUnload; force-unregistering");
				sm.unregisterService(id);
			}
		}
	}
} // namespace

PluginManager::~PluginManager()
{
	for (auto& [path, entry] : m_plugins)
	{
		std::string name = entry.plugin
		                       ? std::string(static_cast<std::string_view>(entry.plugin->name()))
		                       : path;
		if (entry.plugin)
			entry.plugin->onUnload(m_sm);
		sweepSurvivingServices(m_sm, entry.serviceIds, name);
	}
	m_plugins.clear();
}

std::string PluginManager::resolveCanonical(std::string const& path)
{
	std::error_code ec;
	auto c = std::filesystem::canonical(path, ec);
	return ec ? std::string{} : c.string();
}

Result<void, Error> PluginManager::checkRequirements(ServiceManager const&          sm,
                                                     Span<const ServiceRequirement> reqs)
{
	for (std::size_t i = 0; i < reqs.size(); ++i)
	{
		auto const& req = reqs[i];
		auto svc = sm.getService<IService>(req.id);
		if (!svc)
			return Result<void, Error>::err({ErrorCode::NotLoaded,
				std::string("plugin requires service '") + req.id.name()
				+ "' which is not registered"});

		if (!Version::compatible(req.version, svc->version()))
		{
			std::string msg = std::string("plugin requires service '") + req.id.name() + "' at "
			    + toString(req.version)
			    + "; registered version is "
			    + toString(svc->version());
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
	Registry::instance().pluginGarbage().collect();

	auto canonical = resolveCanonical(path);
	if (canonical.empty())
		return Result<OpenedPlugin, Error>::err({ErrorCode::FileNotFound,
			"Cannot resolve path: " + path});

	if (m_plugins.count(canonical))
		return Result<OpenedPlugin, Error>::err({ErrorCode::AlreadyLoaded,
			"Plugin already loaded: " + canonical});

	auto openResult = PluginHandle::open(canonical);
	if (!openResult)
		return Result<OpenedPlugin, Error>::err(openResult.error());

	PluginHandle handle = std::move(openResult.value());

	auto* destroy = handle.destroyFn();
	IPlugin* raw  = handle.createFn()();
	if (!raw)
		return Result<OpenedPlugin, Error>::err({ErrorCode::FactoryFailed,
			"thx_create_plugin returned null for: " + canonical});

	// Wrap in shared_ptr so destruction routes back through the DSO's destroy
	// function. The destroy function pointer remains valid while the handle is
	// alive — OpenedPlugin's destruction order (m_plugin before m_handle)
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
	if (m_plugins.count(opened.m_canonical))
		return Result<void, Error>::err({ErrorCode::AlreadyLoaded,
			"Plugin already loaded: " + opened.m_canonical});

	if (auto r = checkRequirements(m_sm, opened.m_plugin->required()); !r)
		return r;

	// Snapshot the registry so we can attribute new registrations to this plugin.
	auto before = m_sm.listServices();
	std::unordered_set<ServiceID> beforeIds;
	beforeIds.reserve(before.size());
	for (auto const& s : before)
		beforeIds.insert(s.id);

	auto diffNewIds = [&]() -> std::vector<ServiceID>
	{
		std::vector<ServiceID> ids;
		for (auto const& s : m_sm.listServices())
			if (!beforeIds.count(s.id))
				ids.push_back(s.id);
		std::sort(ids.begin(), ids.end(),
		    [](ServiceID const& a, ServiceID const& b)
		    {
		        return std::string_view(a.name()) < std::string_view(b.name());
		    });
		return ids;
	};

	if (!opened.m_plugin->onLoad(m_sm))
	{
		// onLoad may have partially registered services before returning false.
		// Unregister them so the failed load leaves the registry as it was.
		for (auto const& id : diffNewIds())
			m_sm.unregisterService(id);
		return Result<void, Error>::err({ErrorCode::RegistrationFailed,
			"IPlugin::onLoad returned false for: " + opened.m_canonical});
	}

	Entry entry;
	entry.handle      = std::move(opened.m_handle);
	entry.serviceIds = diffNewIds();
	entry.plugin      = std::move(opened.m_plugin);
	m_plugins.emplace(opened.m_canonical, std::move(entry));
	return Result<void, Error>::ok();
}

Result<void, Error> PluginManager::load(std::string const& path)
{
	// Preserve the historical "loading the same path twice is a no-op"
	// behaviour. open() reports AlreadyLoaded as an error; here we swallow it.
	auto canonical = resolveCanonical(path);
	if (!canonical.empty() && m_plugins.count(canonical))
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
	auto canonical = resolveCanonical(path);
	if (canonical.empty())
		return Result<void, Error>::err({ErrorCode::NotLoaded,
			"Plugin not loaded (path cannot be resolved): " + path});

	auto it = m_plugins.find(canonical);
	if (it == m_plugins.end())
		return Result<void, Error>::err({ErrorCode::NotLoaded,
			"Plugin not loaded: " + path});

	std::string name = it->second.plugin
	                       ? std::string(static_cast<std::string_view>(it->second.plugin->name()))
	                       : canonical;
	if (it->second.plugin)
		it->second.plugin->onUnload(m_sm);
	sweepSurvivingServices(m_sm, it->second.serviceIds, name);

	m_plugins.erase(it); // ~Entry: plugin destroyed first, then handle dlclose
	return Result<void, Error>::ok();
}

bool PluginManager::isLoaded(std::string const& path) const
{
	// Map keys are canonical paths; if canonicalization fails the file isn't
	// reachable on disk and therefore can't match any loaded entry.
	auto canonical = resolveCanonical(path);
	if (canonical.empty())
		return false;
	return m_plugins.count(canonical) > 0;
}

Result<void, Error> PluginManager::discover(std::string const& directory)
{
	std::error_code ec;
	auto iter = std::filesystem::directory_iterator(directory, ec);
	if (ec)
		return Result<void, Error>::err({ErrorCode::FileNotFound,
			"Cannot iterate directory '" + directory + "': " + ec.message()});

	for (auto const& entry : iter)
	{
		if (entry.path().extension().string() != LIBRARY_EXTENSION)
			continue;

		auto canonical = resolveCanonical(entry.path().string());
		if (canonical.empty())
			continue; // file vanished between iteration and canonicalize; skip silently

		// Don't disturb an entry that's already Opened or Loaded — those
		// states supersede Discovered.
		if (m_plugins.count(canonical))
			continue;

		// emplace is a no-op if already discovered; that's the intended
		// idempotent re-scan behaviour.
		m_discovered.emplace(canonical, DiscoveredEntry{});
	}
	return Result<void, Error>::ok();
}

Result<void, Error> PluginManager::forget(std::string const& path)
{
	auto canonical = resolveCanonical(path);
	// If the path can't be canonicalized, fall back to the raw input — the
	// file may have been deleted but the entry could still be in our map.
	auto const& key = canonical.empty() ? path : canonical;

	if (m_plugins.count(key))
		return Result<void, Error>::err({ErrorCode::InUse,
			"Cannot forget '" + key + "': still loaded (call unload() first)"});

	m_discovered.erase(key);
	// Either we erased it or it was already gone; both are "ok" — forget is
	// idempotent on absence.
	return Result<void, Error>::ok();
}

PluginManager::LoadSummary PluginManager::discoverAndLoad(std::string const& directory)
{
	LoadSummary summary;
	if (auto r = discover(directory); !r)
	{
		thx::log(LogLevel::Warn,
		    "discoverAndLoad: discover failed for '" + directory + "': " + r.error().message);
		return summary;
	}

	// Snapshot the paths first — load() may move entries between maps as it
	// runs, so iterating m_discovered directly would invalidate.
	std::vector<std::string> paths;
	paths.reserve(m_discovered.size());
	for (auto const& [p, _] : m_discovered)
		paths.push_back(p);
	std::sort(paths.begin(), paths.end()); // deterministic load order

	for (auto const& p : paths)
	{
		auto r = load(p);
		if (r)
		{
			summary.loaded.push_back(p);
		}
		else
		{
			thx::log(LogLevel::Warn,
			    "discoverAndLoad: failed to load '" + p + "': " + r.error().message);
			summary.failed.emplace_back(p, std::move(r.error()));
		}
	}
	return summary;
}

std::vector<LoadedPluginInfo> PluginManager::listPlugins() const
{
	std::vector<LoadedPluginInfo> result;
	result.reserve(m_plugins.size());
	for (auto const& [path, entry] : m_plugins)
	{
		std::string name = entry.plugin
		                       ? std::string(static_cast<std::string_view>(entry.plugin->name()))
		                       : std::string{};
		result.push_back({path, std::move(name), entry.serviceIds});
	}
	return result;
}

// --- Phase 6 query API -------------------------------------------------------
//
// Discovered and Loaded entries are populated. Opened tracking arrives in
// Commit 3.

PluginInfo PluginManager::infoFromEntry(std::string const& path, Entry const& entry) const
{
	PluginInfo info;
	info.path  = path;
	info.state = State::Loaded;
	if (entry.plugin)
	{
		info.name    = std::string(static_cast<std::string_view>(entry.plugin->name()));
		info.version = entry.plugin->version();
		auto reqs    = entry.plugin->required();
		info.requirements.assign(reqs.begin(), reqs.end());
	}
	info.services = entry.serviceIds;
	// `provides` stays empty until Phase 5 manifests populate it.
	return info;
}

namespace
{
	PluginInfo infoFromDiscovered(std::string const& path)
	{
		PluginInfo info;
		info.path  = path;
		info.state = State::Discovered;
		// Other fields are empty: until Phase 5 manifests, a Discovered
		// entry carries no metadata.
		return info;
	}
} // namespace

std::vector<PluginInfo> PluginManager::plugins() const
{
	std::vector<PluginInfo> result;
	result.reserve(m_discovered.size() + m_plugins.size());
	for (auto const& [path, _] : m_discovered)
		result.push_back(infoFromDiscovered(path));
	for (auto const& [path, entry] : m_plugins)
		result.push_back(infoFromEntry(path, entry));
	return result;
}

std::vector<PluginInfo> PluginManager::plugins(State state) const
{
	std::vector<PluginInfo> result;
	switch (state)
	{
		case State::Discovered:
			result.reserve(m_discovered.size());
			for (auto const& [path, _] : m_discovered)
				result.push_back(infoFromDiscovered(path));
			return result;
		case State::Opened:
			// Opened tracking arrives in Commit 3.
			return result;
		case State::Loaded:
			result.reserve(m_plugins.size());
			for (auto const& [path, entry] : m_plugins)
				result.push_back(infoFromEntry(path, entry));
			return result;
	}
	return result;
}

std::optional<PluginInfo> PluginManager::pluginInfo(std::string const& path) const
{
	auto canonical = resolveCanonical(path);
	auto const& key = canonical.empty() ? path : canonical;

	if (auto it = m_plugins.find(key); it != m_plugins.end())
		return infoFromEntry(it->first, it->second);
	if (m_discovered.count(key))
		return infoFromDiscovered(key);
	return std::nullopt;
}

bool PluginManager::is(State state, std::string const& path) const
{
	auto canonical = resolveCanonical(path);
	auto const& key = canonical.empty() ? path : canonical;

	switch (state)
	{
		case State::Discovered:
			return m_discovered.count(key) > 0;
		case State::Opened:
			return false; // Opened tracking arrives in Commit 3.
		case State::Loaded:
			return m_plugins.count(key) > 0;
	}
	return false;
}

} // namespace thx::plugin