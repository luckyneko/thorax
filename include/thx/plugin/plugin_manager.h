/*
 *  Created by LuckyNeko on 24/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "thx/plugin/iplugin.h"
#include "thx/plugin/plugin_garbage.h"
#include "thx/plugin/plugin_handle.h"
#include "thx/result.h"
#include "thx/service/service_id.h"
#include "thx/service/service_manager.h"
#include "thx/log.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace thx::plugin
{
	// Snapshot entry returned by PluginManager::listPlugins().
	struct LoadedPluginInfo
	{
		std::string                            path;
		std::string                            pluginName; // from IPlugin::name()
		std::vector<thx::service::ServiceID>   services;   // all service IDs registered by this plugin
	};

	// Lifecycle state of a plugin tracked by PluginManager.
	//
	// Discovered — filesystem entry has been seen (and, when Phase 5 manifests
	//              land, its sidecar parsed). No DSO interaction yet.
	// Opened     — DSO mapped, IPlugin instantiated, ready for load. onLoad
	//              has NOT been called.
	// Loaded     — onLoad succeeded, services registered.
	enum class State
	{
		Discovered,
		Opened,
		Loaded,
	};

	// Value-typed snapshot of one plugin known to PluginManager. Fields are
	// populated incrementally as the entry progresses through the State
	// machine; consult `state` to know what's actually meaningful.
	//
	// Field availability by state:
	//   path          — always.
	//   state         — always.
	//   name          — Discovered if a manifest provided it, otherwise filled
	//                   in once Opened (from IPlugin::name()).
	//   version       — same.
	//   requirements  — same; from manifest if present, else from IPlugin once
	//                   Opened.
	//   provides      — populated from the manifest when present; once Loaded,
	//                   matches the services actually registered.
	//   services      — empty until Loaded; then the service IDs registered by
	//                   the plugin's onLoad().
	//
	// `requirements` is spelled out instead of `requires` to avoid the C++20
	// concepts keyword.
	struct PluginInfo
	{
		std::string                          path;
		State                                state;
		std::string                          name;
		Version                              version;
		std::vector<ServiceRequirement>      requirements;
		std::vector<thx::service::ServiceID> provides;
		std::vector<thx::service::ServiceID> services;
	};

	// A plugin DSO that has been opened and its IPlugin instantiated, but whose
	// onLoad has NOT yet been called and whose required() services have NOT yet
	// been checked. The caller queries name()/version()/required() to plan load
	// order, then passes the value to PluginManager::load(OpenedPlugin) to
	// commit. Three-step flow: discover -> open -> load.
	//
	// Move-only. A default-constructed or moved-from OpenedPlugin is empty and
	// converts to false.
	//
	// Lifetime: while alive, the DSO is mapped and the IPlugin instance exists.
	// Dropping the value without passing it to load() destroys the IPlugin and
	// queues the DSO into the deferred-close graveyard (drained at the next
	// PluginManager::open() or thx::plugin::collectGarbage()).
	class OpenedPlugin
	{
	public:
		OpenedPlugin() = default;
		~OpenedPlugin();

		OpenedPlugin(OpenedPlugin&&) noexcept;
		OpenedPlugin& operator=(OpenedPlugin&&) noexcept;

		OpenedPlugin(OpenedPlugin const&)            = delete;
		OpenedPlugin& operator=(OpenedPlugin const&) = delete;

		explicit operator bool() const noexcept { return static_cast<bool>(m_plugin); }

		// Canonical filesystem path of the DSO.
		std::string const& path() const noexcept { return m_canonical; }

		// Plugin-reported metadata. Valid once open() has succeeded.
		StringView                     name() const noexcept;
		Version                        version() const noexcept;
		Span<const ServiceRequirement> required() const noexcept;

	private:
		friend class PluginManager;
		OpenedPlugin(PluginHandle handle,
		             std::shared_ptr<IPlugin> plugin,
		             std::string canonical);

		// Destruction order matters: m_plugin (whose destructor lives in plugin
		// code) is reset before m_handle is destroyed.
		PluginHandle             m_handle;
		std::shared_ptr<IPlugin> m_plugin;
		std::string              m_canonical;
	};

	// Loads, unloads, and discovers plugin shared libraries.
	//
	// Owns the DSO handles and integrates with a ServiceManager. Each loaded
	// plugin produces an IPlugin (via THX_DEFINE_SERVICE_PLUGIN or THX_DEFINE_PLUGIN)
	// which registers any number of services in onLoad. The loader tracks
	// canonical paths so loading the same file twice is a safe no-op.
	//
	// Thread safety: not thread-safe. Protect concurrent calls externally if needed.
	// (The deferred-dlclose graveyard used internally is thread-safe.)
	//
	// Destruction: any plugins still loaded when the PluginManager is destroyed
	// are unloaded automatically (services unregistered, DSO handles deferred).
	// The destructor does NOT call collectGarbage(); call it explicitly
	// when no service references into those DSOs remain.
	class PluginManager
	{
	public:
		explicit PluginManager(thx::service::ServiceManager& sm);
		~PluginManager();

		PluginManager(PluginManager const&)            = delete;
		PluginManager& operator=(PluginManager const&) = delete;

		// Opens the DSO at path, ABI-checks it, and instantiates its IPlugin —
		// but does NOT call onLoad and does NOT check required(). Use this to
		// inspect a plugin's name/version/required() before committing to a
		// load order across many plugins.
		//
		// As a side effect, drains the deferred-close queue (see unload). This
		// keeps the queue bounded in long-running programs but means any
		// service references held over from an earlier unload MUST be released
		// before calling open() — otherwise the drain unmaps the DSO out from
		// under them.
		//
		// Returns Err(AlreadyLoaded) if this PluginManager already has the
		// canonical path loaded.
		Result<OpenedPlugin, Error> open(std::string const& path);

		// Completes the load of a previously opened plugin: checks required()
		// against the registry, calls IPlugin::onLoad, and takes ownership of
		// the DSO + IPlugin on success.
		//
		// The OpenedPlugin is consumed either way; on failure its DSO is
		// released to the graveyard at the next drain.
		//
		// Returns Err(AlreadyLoaded) if another entry with the same canonical
		// path has appeared since the plugin was opened.
		Result<void, Error> load(OpenedPlugin opened);

		// Convenience: opens the DSO at path and immediately loads it.
		// If the canonical path is already loaded, returns ok (no-op).
		// Equivalent to a chained open() + load(OpenedPlugin), except that
		// duplicate paths are treated as a successful no-op rather than an
		// AlreadyLoaded error.
		Result<void, Error> load(std::string const& path);

		// Dry-runs the requirement check that load(OpenedPlugin) would perform.
		// Returns ok if every requirement is satisfied by a service currently
		// registered in sm at a compatible version, or the first failure.
		// Does not mutate sm.
		static Result<void, Error> checkRequirements(thx::service::ServiceManager const& sm,
		                                              Span<const ServiceRequirement>     reqs);

		// Unregisters the plugin's services and releases the DSO from this loader.
		// Returns Err(NotLoaded) if path was not previously loaded.
		//
		// Lifetime: the DSO is NOT immediately unmapped. Its native handle is
		// pushed onto a process-wide deferred-close queue, drained at the next
		// call to load() or thx::plugin::collectGarbage(). This means callers
		// MAY hold shared_ptr<IService> handles across unload — the DSO stays
		// mapped (and the service's destructor / shared_ptr control block stay
		// reachable) until the next drain. Once collectGarbage() runs,
		// every still-held service reference into the unmapped DSO becomes
		// undefined behaviour, so drain only when no such references remain.
		Result<void, Error> unload(std::string const& path);

		// Returns true if the canonical path is currently loaded.
		bool isLoaded(std::string const& path) const;

		// Scans directory for files whose extension matches the platform plugin
		// extension (.dylib / .so / .dll). Does not load them.
		std::vector<std::string> discover(std::string const& directory) const;

		// Outcome of a discoverAndLoad call: which paths loaded successfully
		// and which failed (with their associated Error). Either list may be
		// empty. Callers can choose how to react to partial failure.
		struct LoadSummary
		{
			std::vector<std::string>                       loaded;
			std::vector<std::pair<std::string, Error>>     failed;
		};

		// Discovers all plugins in directory and loads each one.
		// Always returns a summary; callers inspect loaded/failed to decide
		// what counts as success. Individual failures are also logged via
		// thx::log().
		LoadSummary discoverAndLoad(std::string const& directory);

		// Returns a snapshot of currently loaded plugins and the service ID each
		// registered. Useful for diagnostics and test assertions.
		std::vector<LoadedPluginInfo> listPlugins() const;

		// --- Phase 6 query API ---------------------------------------------
		//
		// These methods will become the canonical inspection surface once the
		// PluginManager-owned-lifecycle reshape lands. For now they coexist
		// with listPlugins(); only the Loaded state is populated, since
		// Discovered/Opened tracking is added in later commits.

		// Returns a snapshot of every plugin known to the manager in any state.
		std::vector<PluginInfo> plugins() const;

		// Returns a snapshot of plugins filtered to a single lifecycle state.
		std::vector<PluginInfo> plugins(State state) const;

		// Returns the snapshot for one plugin by canonical path, or nullopt if
		// the path isn't currently tracked.
		std::optional<PluginInfo> pluginInfo(std::string const& path) const;

		// True if the plugin at `path` is currently in `state`.
		bool is(State state, std::string const& path) const;

	private:
		struct Entry
		{
			// Declaration order matters: `plugin` is destroyed before `handle`,
			// so the IPlugin's destructor (which lives in plugin code) runs
			// before the DSO is dlclose()d.
			PluginHandle                         handle;
			std::vector<thx::service::ServiceID> serviceIds;
			std::shared_ptr<IPlugin>             plugin;
		};

		thx::service::ServiceManager& m_sm;
		std::unordered_map<std::string, Entry> m_plugins; // canonical_path → entry

		static std::string resolveCanonical(std::string const& path);

		// Build a PluginInfo from a Loaded entry. Member function so it can
		// reach Entry, which is private.
		PluginInfo infoFromEntry(std::string const& path, Entry const& entry) const;
	};

} // namespace thx::plugin