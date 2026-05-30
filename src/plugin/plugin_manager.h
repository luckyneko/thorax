/*
 *  Created by LuckyNeko on 24/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "library.h"
#include "plugin/plugin_garbage.h"
#include "plugin/plugin_handle.h"
#include "thx/plugin/iplugin.h"
#include "thx/plugin/manifest.h"
#include "thx/plugin/plugin.h"          // PluginInfo, State, LoadSummary
#include "thx/result.h"
#include "thx/service/service_id.h"
#include "service/service_manager.h"
#include "thx_internal_api.h"
#include "thx/log.h"

#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace thx::plugin
{
	// Loads, unloads, and tracks plugin shared libraries by canonical path
	// across three lifecycle states (see State enum). All state lives inside
	// PluginManager; callers only see value-typed PluginInfo snapshots and
	// Result<void, Error> outcomes.
	//
	// Thread safety: every public method takes a recursive mutex covering the
	// three lifecycle maps. Reentrant calls from inside a plugin's onLoad /
	// onUnload (e.g., a plugin that loads a sibling) work because the lock is
	// recursive. The lock is coarse — concurrent loads of independent plugins
	// serialize — but plugin loading is off the hot path so this is fine. The
	// deferred-dlclose garbage queue is independently thread-safe.
	//
	// Destruction: any plugins still Loaded when the PluginManager is destroyed
	// are unloaded automatically (services unregistered, DSO handles deferred);
	// Opened-but-not-Loaded entries have their DSOs released to the garbage
	// queue. The destructor does NOT call collectGarbage(); call it explicitly
	// when no service references into those DSOs remain.
	class THX_INTERNAL_API PluginManager
	{
	public:
		// `sm` MUST be the Registry's ServiceManager. A plugin's onLoad/onUnload
		// registers its services through the thx::service::* facades, which
		// dispatch to Registry::instance().serviceManager(); finalizeLoad then
		// attributes the newly-registered services by diffing `sm`. The two only
		// agree when sm IS the Registry's ServiceManager. In production the
		// Registry constructs the PluginManager with its own ServiceManager; the
		// injected reference exists so that construction can happen in the right
		// order (ServiceManager before PluginManager), not to support alternate
		// ServiceManagers.
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
		// as a Discovered entry. With Recursive::Yes, walks subdirectories
		// too — useful when plugins are organised in per-category folders.
		// Idempotent: re-scanning leaves existing Opened / Loaded entries
		// untouched and silently skips already-known Discovered paths.
		// Returns FileNotFound if the directory cannot be iterated.
		Result<void, Error> discover(std::string const& directory,
		                              Recursive recursive = Recursive::No);

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

		// Resets the manager to empty: unloads every Loaded plugin (onUnload +
		// service cleanup), drops every Opened entry, and forgets every
		// Discovered entry. Each plugin's DSO is queued to PluginGarbage, NOT
		// closed here — drain the queue (collectGarbage) once no ServiceHandle
		// into those DSOs remains. This is the body of ~PluginManager, exposed
		// so thx::shutdown() can return the process-wide manager to an empty
		// state without destroying the Registry-owned instance.
		//
		// PRECONDITION (same as unload/reload): release every ServiceHandle
		// obtained from these plugins' services before the subsequent drain.
		void clear();

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

		// unload() + collectGarbage() + load(). Convenience for the common
		// reload pattern.
		//
		// PRECONDITION: caller has released every ServiceHandle it obtained
		// from this plugin's services BEFORE calling reload. The collect
		// runs synchronously and dlclose's the DSO; any outstanding handles
		// will segfault on release once their refcount hits zero (the
		// service destructor lives in unmapped code).
		Result<void, Error> reload(std::string const& path);

		// --- Aggregate ----------------------------------------------------

		// Discovers all plugins in `directory` and loads each one.
		// Returns a summary; individual failures are also logged.
		LoadSummary discoverAndLoad(std::string const& directory,
		                             Recursive recursive = Recursive::No);

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

		// Filter: plugins (any state) whose manifest `provides` contains
		// `serviceId`. Manifest data is read at discover(); no DSO interaction.
		std::vector<PluginInfo> pluginsProviding(std::string const& serviceId) const;

	private:
		// Each entry carries its PluginManifest through every state transition
		// so callers can query the static declaration regardless of where the
		// plugin sits in the lifecycle. The manifest is also what
		// `finalizeLoad` cross-checks against the live IPlugin at load time
		// (see Phase 5 Commit 3).

		// Loaded entry: owns the DSO + IPlugin plus the service IDs it
		// registered.
		//
		// Declaration order matters — fields destruct in reverse:
		//   `plugin` (shared_ptr<IPlugin>) → IPlugin dtor runs in DSO code.
		//   `serviceIds` (vector<ServiceID>) → ServiceID is trivial, but each
		//     `m_name` pointer may point into DSO static storage (the
		//     `kProvides` array in ServicePluginShim<T>::provides() lives in
		//     the plugin DSO). Destructing serviceIds while the DSO is still
		//     mapped is harmless; the vector just deallocates its own backing
		//     storage. Reading any ServiceID's name() after this point would
		//     be UB — nothing in PluginManager does that.
		//   `handle` (PluginHandle) → schedules the DSO to the deferred-close
		//     queue (PluginGarbage), so dlclose runs later, not here.
		//   `manifest` → plain value, no DSO dependency.
		// The key invariant: the IPlugin destructor (plugin) runs before the
		// DSO is queued for close (handle).
		struct LoadedEntry
		{
			PluginManifest                       manifest;
			PluginHandle                         handle;
			std::vector<thx::service::ServiceID> serviceIds;
			std::shared_ptr<IPlugin>             plugin;
		};

		// Opened-but-not-Loaded entry: the DSO is mapped and an IPlugin
		// exists, but onLoad has not been called. Same destruction-order
		// rationale as LoadedEntry.
		struct OpenedEntry
		{
			PluginManifest           manifest;
			PluginHandle             handle;
			std::shared_ptr<IPlugin> plugin;
		};

		// Discovered entry: manifest read from the sidecar, DSO not yet
		// opened.
		struct DiscoveredEntry
		{
			PluginManifest manifest;
		};

		thx::service::ServiceManager& m_sm;
		mutable std::recursive_mutex                     m_mutex;
		std::unordered_map<std::string, DiscoveredEntry> m_discovered;
		std::unordered_map<std::string, OpenedEntry>     m_opened;
		std::unordered_map<std::string, LoadedEntry>     m_plugins;
		// Sidecar paths we've already warned about for having no paired DSO.
		// discover() only warns on first observation per path; rescans skip
		// silently so a known-broken pair doesn't spam logs.
		std::unordered_set<std::string>                  m_warnedMissingDso;

		static std::string resolveCanonical(std::string const& path);

		// Compute the sidecar manifest path paired with a DSO path: strip
		// LIBRARY_EXTENSION, append ".thx.json".
		static std::string manifestPathForDso(std::string const& dsoPath);

		// Open a DSO at `canonical` and produce an OpenedEntry (without the
		// manifest — caller supplies it). Used by both open() and the
		// implicit-open path inside load(). Drains the garbage queue first.
		Result<OpenedEntry, Error> openHandle(std::string const& canonical,
		                                      PluginManifest manifest);

		// Promote an OpenedEntry into a LoadedEntry by checking required(),
		// calling onLoad, and attributing the resulting service IDs.
		Result<LoadedEntry, Error> finalizeLoad(OpenedEntry opened,
		                                        std::string const& canonical);

		// Ensure the path has a Discovered entry by locating and parsing
		// its sidecar manifest. No-op if the path already has a Discovered
		// / Opened / Loaded entry. Used by open()/load() to support the
		// "load by direct path without prior discover" shortcut.
		Result<void, Error> ensureDiscovered(std::string const& canonical);

		// Build PluginInfo snapshots from internal state.
		PluginInfo infoFromDiscovered(std::string const& path, DiscoveredEntry const& entry) const;
		PluginInfo infoFromOpened    (std::string const& path, OpenedEntry     const& entry) const;
		PluginInfo infoFromLoaded    (std::string const& path, LoadedEntry     const& entry) const;
	};

} // namespace thx::plugin
