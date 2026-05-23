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

// Bring the service-layer types we touch heavily into scope so the
// implementation reads the same as before the namespace split. The public
// header still uses fully-qualified names.
using thx::service::IService;
using thx::service::ServiceID;
using thx::service::ServiceManager;

PluginManager::PluginManager(ServiceManager& sm) : m_sm(sm) {}

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

	std::string pluginDisplayName(std::shared_ptr<IPlugin> const& plugin,
	                              std::string const&              fallback)
	{
		return plugin
		    ? std::string(static_cast<std::string_view>(plugin->name()))
		    : fallback;
	}
} // namespace

PluginManager::~PluginManager()
{
	// Loaded entries first: onUnload + service cleanup, then ~LoadedEntry
	// schedules the DSO to the garbage queue.
	for (auto& [path, entry] : m_plugins)
	{
		auto name = pluginDisplayName(entry.plugin, path);
		if (entry.plugin)
			entry.plugin->onUnload(m_sm);
		sweepSurvivingServices(m_sm, entry.serviceIds, name);
	}
	m_plugins.clear();
	// Opened-but-not-Loaded entries: just drop them. ~OpenedEntry destroys
	// the IPlugin and queues the DSO to the garbage queue. No services to
	// unregister.
	m_opened.clear();
	// Discovered entries hold nothing; clearing is implicit.
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

// --- Internal: open a DSO and produce an OpenedEntry -------------------------

Result<PluginManager::OpenedEntry, Error>
PluginManager::openHandle(std::string const& canonical)
{
	// Drain the deferred-close queue before any new dlopen so we don't
	// accumulate a long tail of mapped-but-released DSOs in long-running
	// processes.
	Registry::instance().pluginGarbage().collect();

	auto handleResult = PluginHandle::open(canonical);
	if (!handleResult)
		return Result<OpenedEntry, Error>::err(handleResult.error());

	PluginHandle handle = std::move(handleResult.value());

	auto* destroy = handle.destroyFn();
	IPlugin* raw  = handle.createFn()();
	if (!raw)
		return Result<OpenedEntry, Error>::err({ErrorCode::FactoryFailed,
			"thx_create_plugin returned null for: " + canonical});

	// Wrap in shared_ptr so destruction routes back through the DSO's destroy
	// function. The destroy function pointer remains valid while the handle
	// is alive — OpenedEntry's declaration order (plugin before handle in
	// the struct, hence destroyed first) guarantees that.
	std::shared_ptr<IPlugin> plugin(raw, [destroy](IPlugin* p)
	{
		if (p && destroy)
			destroy(p);
	});

	OpenedEntry entry;
	entry.handle = std::move(handle);
	entry.plugin = std::move(plugin);
	return Result<OpenedEntry, Error>::ok(std::move(entry));
}

// --- Internal: promote an OpenedEntry into a LoadedEntry ---------------------

Result<PluginManager::LoadedEntry, Error>
PluginManager::finalizeLoad(OpenedEntry opened, std::string const& canonical)
{
	if (auto r = checkRequirements(m_sm, opened.plugin->required()); !r)
		return Result<LoadedEntry, Error>::err(std::move(r.error()));

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

	if (!opened.plugin->onLoad(m_sm))
	{
		// onLoad may have partially registered services before returning false.
		// Unregister them so the failed load leaves the registry as it was.
		for (auto const& id : diffNewIds())
			m_sm.unregisterService(id);
		return Result<LoadedEntry, Error>::err({ErrorCode::RegistrationFailed,
			"IPlugin::onLoad returned false for: " + canonical});
	}

	LoadedEntry entry;
	entry.handle     = std::move(opened.handle);
	entry.serviceIds = diffNewIds();
	entry.plugin     = std::move(opened.plugin);
	return Result<LoadedEntry, Error>::ok(std::move(entry));
}

// --- Lifecycle ---------------------------------------------------------------

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

		// Don't disturb entries that are already Opened or Loaded — those
		// states supersede Discovered.
		if (m_plugins.count(canonical) || m_opened.count(canonical))
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
	auto const& key = canonical.empty() ? path : canonical;

	if (m_plugins.count(key))
		return Result<void, Error>::err({ErrorCode::InUse,
			"Cannot forget '" + key + "': still loaded (call unload() first)"});
	if (m_opened.count(key))
		return Result<void, Error>::err({ErrorCode::InUse,
			"Cannot forget '" + key + "': still opened (call close() first)"});

	m_discovered.erase(key);
	// Either we erased it or it was already gone; both are "ok" — forget is
	// idempotent on absence.
	return Result<void, Error>::ok();
}

Result<void, Error> PluginManager::open(std::string const& path)
{
	auto canonical = resolveCanonical(path);
	if (canonical.empty())
		return Result<void, Error>::err({ErrorCode::FileNotFound,
			"Cannot resolve path: " + path});

	// Idempotent: Loaded supersedes Opened; Opened already in place is ok.
	if (m_plugins.count(canonical) || m_opened.count(canonical))
		return Result<void, Error>::ok();

	auto entryResult = openHandle(canonical);
	if (!entryResult)
		return Result<void, Error>::err(std::move(entryResult.error()));

	// Successful open: remove any Discovered shadow and install the OpenedEntry.
	m_discovered.erase(canonical);
	m_opened.emplace(canonical, std::move(entryResult.value()));
	return Result<void, Error>::ok();
}

Result<void, Error> PluginManager::close(std::string const& path)
{
	auto canonical = resolveCanonical(path);
	auto const& key = canonical.empty() ? path : canonical;

	auto it = m_opened.find(key);
	if (it == m_opened.end())
		return Result<void, Error>::ok(); // not Opened — no-op (idempotent)

	// Drop the OpenedEntry — its IPlugin and PluginHandle (Library) are
	// destroyed in declaration order, queuing the DSO to the garbage queue.
	m_opened.erase(it);
	// Per the spec, close() always returns the entry to Discovered.
	m_discovered.emplace(key, DiscoveredEntry{});
	return Result<void, Error>::ok();
}

std::size_t PluginManager::closeAllOpened()
{
	std::size_t count = m_opened.size();
	for (auto& [path, _] : m_opened)
		m_discovered.emplace(path, DiscoveredEntry{});
	m_opened.clear();
	return count;
}

Result<void, Error> PluginManager::load(std::string const& path)
{
	auto canonical = resolveCanonical(path);
	if (canonical.empty())
		return Result<void, Error>::err({ErrorCode::FileNotFound,
			"Cannot resolve path: " + path});

	// Idempotent: already loaded → ok no-op.
	if (m_plugins.count(canonical))
		return Result<void, Error>::ok();

	// Extract or build the OpenedEntry. If the path is already Opened, take
	// the existing entry; otherwise open it implicitly.
	OpenedEntry opened;
	if (auto it = m_opened.find(canonical); it != m_opened.end())
	{
		opened = std::move(it->second);
		m_opened.erase(it);
	}
	else
	{
		auto openedResult = openHandle(canonical);
		if (!openedResult)
			return Result<void, Error>::err(std::move(openedResult.error()));
		opened = std::move(openedResult.value());
	}

	auto loaded = finalizeLoad(std::move(opened), canonical);
	if (!loaded)
	{
		// finalizeLoad consumed `opened`; the OpenedEntry it built is now
		// gone (DSO queued to garbage). The Discovered shadow, if any, is
		// untouched — callers can retry or forget().
		return Result<void, Error>::err(std::move(loaded.error()));
	}

	// Successful load: remove any Discovered shadow and install the LoadedEntry.
	m_discovered.erase(canonical);
	m_plugins.emplace(canonical, std::move(loaded.value()));
	return Result<void, Error>::ok();
}

Result<void, Error> PluginManager::unload(std::string const& path)
{
	auto canonical = resolveCanonical(path);
	auto const& key = canonical.empty() ? path : canonical;

	auto it = m_plugins.find(key);
	if (it == m_plugins.end())
		return Result<void, Error>::err({ErrorCode::NotLoaded,
			"Plugin not loaded: " + path});

	auto name = pluginDisplayName(it->second.plugin, key);
	if (it->second.plugin)
		it->second.plugin->onUnload(m_sm);
	sweepSurvivingServices(m_sm, it->second.serviceIds, name);

	m_plugins.erase(it); // ~LoadedEntry queues DSO to garbage
	// Per the spec, unload() returns the entry to Discovered.
	m_discovered.emplace(key, DiscoveredEntry{});
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

	// Snapshot the paths first — load() moves entries between maps as it
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

// --- PluginInfo construction -------------------------------------------------

PluginInfo PluginManager::infoFromLoaded(std::string const& path, LoadedEntry const& entry) const
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

PluginInfo PluginManager::infoFromOpened(std::string const& path, OpenedEntry const& entry) const
{
	PluginInfo info;
	info.path  = path;
	info.state = State::Opened;
	if (entry.plugin)
	{
		info.name    = std::string(static_cast<std::string_view>(entry.plugin->name()));
		info.version = entry.plugin->version();
		auto reqs    = entry.plugin->required();
		info.requirements.assign(reqs.begin(), reqs.end());
	}
	// services stays empty: onLoad hasn't been called yet.
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

// --- Queries -----------------------------------------------------------------

std::vector<PluginInfo> PluginManager::plugins() const
{
	std::vector<PluginInfo> result;
	result.reserve(m_discovered.size() + m_opened.size() + m_plugins.size());
	for (auto const& [path, _] : m_discovered)
		result.push_back(infoFromDiscovered(path));
	for (auto const& [path, entry] : m_opened)
		result.push_back(infoFromOpened(path, entry));
	for (auto const& [path, entry] : m_plugins)
		result.push_back(infoFromLoaded(path, entry));
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
			result.reserve(m_opened.size());
			for (auto const& [path, entry] : m_opened)
				result.push_back(infoFromOpened(path, entry));
			return result;
		case State::Loaded:
			result.reserve(m_plugins.size());
			for (auto const& [path, entry] : m_plugins)
				result.push_back(infoFromLoaded(path, entry));
			return result;
	}
	return result;
}

std::optional<PluginInfo> PluginManager::pluginInfo(std::string const& path) const
{
	auto canonical = resolveCanonical(path);
	auto const& key = canonical.empty() ? path : canonical;

	if (auto it = m_plugins.find(key); it != m_plugins.end())
		return infoFromLoaded(it->first, it->second);
	if (auto it = m_opened.find(key); it != m_opened.end())
		return infoFromOpened(it->first, it->second);
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
		case State::Discovered: return m_discovered.count(key) > 0;
		case State::Opened:     return m_opened.count(key)     > 0;
		case State::Loaded:     return m_plugins.count(key)    > 0;
	}
	return false;
}

} // namespace thx::plugin
