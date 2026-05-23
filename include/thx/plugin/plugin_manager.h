/*
 *  Created by LuckyNeko on 24/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "thx/library.h"
#include "thx/plugin/iplugin.h"
#include "thx/plugin/plugin_garbage.h"
#include "thx/plugin/plugin_handle.h"
#include "thx/result.h"
#include "thx/service/service_id.h"
#include "thx/service/service_manager.h"
#include "thx/log.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace thx::plugin
{
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

	// Loads, unloads, and tracks plugin shared libraries by canonical path
	// across three lifecycle states (see State enum). All state lives inside
	// PluginManager; callers only see value-typed PluginInfo snapshots and
	// Result<void, Error> outcomes.
	//
	// Thread safety: not thread-safe. Protect concurrent calls externally if
	// needed. (The deferred-dlclose garbage queue used internally is thread-safe.)
	//
	// Destruction: any plugins still Loaded when the PluginManager is destroyed
	// are unloaded automatically (services unregistered, DSO handles deferred);
	// Opened-but-not-Loaded entries have their DSOs released to the garbage
	// queue. The destructor does NOT call collectGarbage(); call it explicitly
	// when no service references into those DSOs remain.
	class PluginManager
	{
	public:
		explicit PluginManager(thx::service::ServiceManager& sm);
		~PluginManager();

		PluginManager(PluginManager const&)            = delete;
		PluginManager& operator=(PluginManager const&) = delete;

		// --- Lifecycle (state mutators) ------------------------------------
		//
		// All take a path by value or reference, all return Result<void, Error>.
		// Each method's documented state transition is enforced; transitions
		// not listed are either no-ops (idempotent) or return an error.

		// Scans `directory` for files matching LIBRARY_EXTENSION and adds each
		// as a Discovered entry. Idempotent: re-scanning leaves existing
		// Opened / Loaded entries untouched and silently skips already-known
		// Discovered paths. Returns FileNotFound if the directory cannot be
		// iterated.
		Result<void, Error> discover(std::string const& directory);

		// Removes a Discovered entry. Returns InUse if the path is Opened or
		// Loaded (caller must close() / unload() first). Idempotent on absence
		// — forgetting an unknown path returns ok.
		Result<void, Error> forget(std::string const& path);

		// Transitions a plugin into Opened: opens the DSO, ABI-checks it, and
		// instantiates the IPlugin. Does NOT call onLoad and does NOT verify
		// required().
		//
		// Allowed source states:
		//   (nothing)  — opens directly (no prior discover required).
		//   Discovered — removes the Discovered entry and adds an Opened one.
		//   Opened     — no-op (idempotent).
		//   Loaded     — no-op (Loaded supersedes Opened).
		//
		// As a side effect, drains the deferred-close queue first. Callers
		// holding shared_ptr<IService> handles from a previously unloaded
		// plugin MUST release them before calling open().
		Result<void, Error> open(std::string const& path);

		// Drops an Opened entry back to Discovered: the IPlugin is destroyed
		// and the DSO is queued for deferred close. No-op if the path is not
		// Opened (idempotent).
		Result<void, Error> close(std::string const& path);

		// Drops every Opened-but-not-Loaded entry back to Discovered. Returns
		// the number of entries closed. Useful after a batch open/inspect
		// phase where only a subset will be loaded.
		std::size_t closeAllOpened();

		// Transitions a plugin into Loaded: checks required(), calls onLoad,
		// and registers the plugin's services.
		//
		// Allowed source states:
		//   (nothing)  — implicit open + load.
		//   Discovered — implicit open + load.
		//   Opened     — loads the existing Opened entry.
		//   Loaded     — no-op (idempotent).
		Result<void, Error> load(std::string const& path);

		// Transitions a Loaded plugin back to Discovered: calls onUnload, the
		// plugin's services are unregistered, and the DSO is queued for
		// deferred close. Returns NotLoaded if the path is not currently
		// Loaded.
		Result<void, Error> unload(std::string const& path);

		// --- Aggregate ----------------------------------------------------

		// Outcome of a discoverAndLoad call: which paths loaded successfully
		// and which failed (with their associated Error). Either list may be
		// empty.
		struct LoadSummary
		{
			std::vector<std::string>                       loaded;
			std::vector<std::pair<std::string, Error>>     failed;
		};

		// Discovers all plugins in `directory` and loads each one.
		// Returns a summary; individual failures are also logged.
		LoadSummary discoverAndLoad(std::string const& directory);

		// Dry-runs the requirement check that load() would perform. Does not
		// mutate sm.
		static Result<void, Error> checkRequirements(thx::service::ServiceManager const& sm,
		                                              Span<const ServiceRequirement>     reqs);

		// --- Queries ------------------------------------------------------

		// Snapshot of every plugin known to the manager in any state.
		std::vector<PluginInfo> plugins() const;

		// Snapshot filtered to a single lifecycle state.
		std::vector<PluginInfo> plugins(State state) const;

		// Snapshot for one plugin by path, or nullopt if untracked.
		std::optional<PluginInfo> pluginInfo(std::string const& path) const;

		// True if the plugin at `path` is currently in `state`.
		bool is(State state, std::string const& path) const;

		// Convenience aliases for the most common state checks.
		bool isDiscovered(std::string const& path) const { return is(State::Discovered, path); }
		bool isOpened    (std::string const& path) const { return is(State::Opened,     path); }
		bool isLoaded    (std::string const& path) const { return is(State::Loaded,     path); }

	private:
		// Loaded entry: owns the DSO + IPlugin plus the service IDs it
		// registered. Declaration order matters — `plugin` is destroyed before
		// `handle`, so the IPlugin destructor (which lives in DSO code) runs
		// before the DSO is dlclose()d.
		struct LoadedEntry
		{
			PluginHandle                         handle;
			std::vector<thx::service::ServiceID> serviceIds;
			std::shared_ptr<IPlugin>             plugin;
		};

		// Opened-but-not-Loaded entry: the DSO is mapped and an IPlugin
		// exists, but onLoad has not been called. Same destruction-order
		// rationale as LoadedEntry.
		struct OpenedEntry
		{
			PluginHandle             handle;
			std::shared_ptr<IPlugin> plugin;
		};

		// Discovered entry placeholder. Phase 5 manifests will populate name,
		// version, requirements, and provides here so Discovered entries
		// carry metadata without a dlopen.
		struct DiscoveredEntry
		{
		};

		thx::service::ServiceManager& m_sm;
		std::unordered_map<std::string, DiscoveredEntry> m_discovered;
		std::unordered_map<std::string, OpenedEntry>     m_opened;
		std::unordered_map<std::string, LoadedEntry>     m_plugins;

		static std::string resolveCanonical(std::string const& path);

		// Open a DSO at `canonical` and produce an OpenedEntry. Used by both
		// open() and the implicit-open path inside load(). Drains the garbage
		// queue first.
		Result<OpenedEntry, Error> openHandle(std::string const& canonical);

		// Promote an OpenedEntry into a LoadedEntry by checking required(),
		// calling onLoad, and attributing the resulting service IDs.
		Result<LoadedEntry, Error> finalizeLoad(OpenedEntry opened,
		                                        std::string const& canonical);

		// Build PluginInfo snapshots from internal state.
		PluginInfo infoFromLoaded(std::string const& path, LoadedEntry const& entry) const;
		PluginInfo infoFromOpened(std::string const& path, OpenedEntry const& entry) const;
	};

} // namespace thx::plugin
