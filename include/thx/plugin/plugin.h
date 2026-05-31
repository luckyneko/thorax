/*
 *  Created by LuckyNeko on 22/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "thx/plugin/iplugin.h"
#include "thx/plugin/manifest.h"
#include "thx/result.h"
#include "thx/span.h"
#include "thx/thx_api.h"
#include "thx/version_type.h"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Public plugin-layer surface. Free functions forward to the framework's
// internal PluginManager (owned by the process-wide Registry singleton). The
// PluginManager class itself is an implementation detail and lives in
// src/plugin/plugin_manager.h — consumers never see it.

namespace thx::plugin
{
	// Whether discover() / discoverAndLoad() should walk subdirectories.
	enum class Recursive
	{
		No,
		Yes,
	};

	// Lifecycle state of a plugin tracked by the framework.
	//
	// Discovered — filesystem entry has been seen and its sidecar parsed. No
	//              DSO interaction yet.
	// Opened     — DSO mapped, IPlugin instantiated, ready for load. onLoad
	//              has NOT been called.
	// Loaded     — onLoad succeeded, services registered.
	enum class State
	{
		Discovered,
		Opened,
		Loaded,
	};

	// Value-typed snapshot of one plugin known to the framework. Fields are
	// populated incrementally as the entry progresses through the State
	// machine; consult `state` to know what's actually meaningful.
	//
	// Field availability by state:
	//   path          — always.
	//   state         — always.
	//   name          — populated from the manifest once Discovered, verified
	//                   against IPlugin::name() at Loaded.
	//   version       — same.
	//   requirements  — populated from the manifest once Discovered, verified
	//                   against IPlugin::required() at Loaded.
	//   provides      — populated from the manifest once Discovered, verified
	//                   against the services actually registered at Loaded.
	//   services      — empty until Loaded; then the service IDs registered by
	//                   the plugin's onLoad(), as strings.
	//
	// All ID fields are stored as std::string (not thx::service::ServiceID)
	// because ServiceID is designed around string-literal lifetimes and a
	// value-typed snapshot can't safely carry literal-backed pointers across
	// copies / moves. Compare against a ServiceID via its .name() accessor.
	//
	// `requirements` is spelled out instead of `requires` to avoid the C++20
	// concepts keyword.
	struct PluginInfo
	{
		std::string path;
		State state;
		std::string name;
		Version version;
		std::vector<ManifestRequirement> requirements;
		std::vector<std::string> provides;
		std::vector<std::string> services;
	};

	// Outcome of a discoverAndLoad call.
	//
	// `loaded`        — every path currently in Loaded state after the call.
	//                   Includes both newly-loaded paths and paths that were
	//                   already Loaded before the call (load() is idempotent).
	// `alreadyLoaded` — subset of `loaded` whose entries existed in Loaded
	//                   state before the call. Empty on the first call;
	//                   non-empty when a second discoverAndLoad sees the same
	//                   plugins still loaded.
	// `failed`        — paths whose load() returned an error, paired with it.
	//
	// Any of the three lists may be empty.
	struct LoadSummary
	{
		std::vector<std::string> loaded;
		std::vector<std::string> alreadyLoaded;
		std::vector<std::pair<std::string, Error>> failed;
	};

	// --- Lifecycle (state mutators) ----------------------------------------
	// All take a path by value or reference; all return Result<void, Error>.
	// Documented behaviour matches the previous PluginManager methods.

	THX_API Result<void, Error> discover(std::string const& directory, Recursive recursive = Recursive::No);
	THX_API Result<void, Error> forget(std::string const& path);
	THX_API Result<void, Error> open(std::string const& path);
	THX_API Result<void, Error> close(std::string const& path);
	THX_API std::size_t closeAllOpened();
	THX_API Result<void, Error> load(std::string const& path);
	THX_API Result<void, Error> unload(std::string const& path);

	// Unload then load again. Equivalent to:
	//
	//     unload(path);
	//     collectGarbage();          // drain the DSO mapping
	//     load(path);
	//
	// Returns NotLoaded if `path` isn't currently Loaded.
	//
	// **Precondition**: the caller MUST have released every ServiceHandle
	// it obtained from this plugin's services BEFORE calling reload. The
	// drain runs synchronously and will dlclose the DSO; any handles still
	// pointing at services in that DSO will segfault when they're released
	// (the service's destructor lives in unmapped code). The framework
	// cannot detect this — it's a discipline contract documented here.
	THX_API Result<void, Error> reload(std::string const& path);

	// Inspect a plugin DSO without requiring a sidecar manifest. Opens the
	// DSO, ABI-checks it, instantiates the IPlugin, reads its name /
	// version / required() / provides(), tears the IPlugin down, and
	// queues the DSO for deferred close. The plugin is NOT loaded — no
	// onLoad is called, no services are registered, no state is tracked.
	//
	// Use case: build-time tooling that derives a sidecar manifest from a
	// freshly-built plugin DSO (thx_emit_manifest). Production code should
	// stick to discover()/load(), which require a paired sidecar as the
	// marker that the DSO is intended as a plugin.
	//
	// Returns the PluginManifest the plugin would advertise. Errors:
	//   OpenFailed       — dlopen / LoadLibrary rejected the DSO.
	//   SymbolNotFound   — missing one of the thx_* C exports.
	//   VersionMismatch  — incompatible thx_abi_version.
	//   FactoryFailed    — thx_create_plugin returned null.
	THX_API Result<PluginManifest, Error> inspect(std::string const& dsoPath);

	// --- Aggregate ---------------------------------------------------------
	THX_API LoadSummary discoverAndLoad(std::string const& directory, Recursive recursive = Recursive::No);

	// Load `path` plus every plugin needed to satisfy its (transitive)
	// manifest requirements, in dependency order. Providers are resolved from
	// the plugins the framework currently knows about (Discovered / Opened /
	// Loaded); a requirement already satisfied by a registered service needs
	// no plugin. If `path` isn't yet known, it is discovered implicitly (its
	// `<basename>.thx.json` sidecar must exist, mirroring load()).
	//
	// Returns a LoadSummary. `failed` carries:
	//   UnresolvedDependency — a requirement that no known plugin provides and
	//                          that is not already registered.
	//   DependencyCycle      — the requires/provides graph contains a cycle.
	// A plugin whose dependency could not be resolved or loaded is itself
	// reported in `failed` and is not loaded.
	THX_API LoadSummary loadWithDependencies(std::string const& path);

	// Load a set of plugins (e.g. plugins(State::Discovered) filtered by
	// pluginsProviding) plus their transitive dependencies, in dependency
	// order. Same resolution rules and summary semantics as
	// loadWithDependencies; the whole set is topo-sorted together so
	// inter-dependencies among the roots are ordered correctly.
	THX_API LoadSummary loadAll(Span<const PluginInfo> plugins);

	// Dry-runs the requirement check that load() would perform against the
	// framework's internal ServiceManager.
	THX_API Result<void, Error> checkRequirements(Span<const ServiceRequirement> reqs);

	// --- Queries -----------------------------------------------------------
	THX_API std::vector<PluginInfo> plugins();
	THX_API std::vector<PluginInfo> plugins(State state);
	THX_API std::optional<PluginInfo> pluginInfo(std::string const& path);
	THX_API bool is(State state, std::string const& path);
	THX_API bool isDiscovered(std::string const& path);
	THX_API bool isOpened(std::string const& path);
	THX_API bool isLoaded(std::string const& path);

	// Returns plugins (any state) whose manifest `provides` list contains
	// the given service id. Useful for picking out e.g. all camera drivers
	// without dlopen-ing anything: the manifest is read at discover() time.
	// Match is by ServiceID name (the string form), since manifest-derived
	// service ids are stored as strings.
	THX_API std::vector<PluginInfo> pluginsProviding(std::string const& serviceId);

	// Type-deduced convenience over pluginsProviding(serviceId): looks the
	// service up by `T`'s stable ID. `T` must derive from
	// thx::service::Service<T> (so it exposes staticId()).
	template <typename T>
	inline std::vector<PluginInfo> pluginsProviding()
	{
		return pluginsProviding(T::staticId().name());
	}

	// First plugin (any state) whose manifest `name` equals `name`, or
	// nullopt if none is known. Manifest data is read at discover() time, so
	// no DSO is opened. Plugin names are not guaranteed unique; if several
	// match, the lexicographically-first canonical path wins.
	THX_API std::optional<PluginInfo> pluginByName(std::string const& name);

	// --- Garbage queue -----------------------------------------------------
	// Drains / queries the framework-owned deferred-dlclose queue.
	THX_API std::size_t collectGarbage() noexcept;
	THX_API std::size_t pendingGarbage() noexcept;

} // namespace thx::plugin
