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
		std::string                      path;
		State                            state;
		std::string                      name;
		Version                          version;
		std::vector<ManifestRequirement> requirements;
		std::vector<std::string>         provides;
		std::vector<std::string>         services;
	};

	// Outcome of a discoverAndLoad call: which paths loaded successfully and
	// which failed (with their associated Error). Either list may be empty.
	struct LoadSummary
	{
		std::vector<std::string>                   loaded;
		std::vector<std::pair<std::string, Error>> failed;
	};

	// --- Lifecycle (state mutators) ----------------------------------------
	// All take a path by value or reference; all return Result<void, Error>.
	// Documented behaviour matches the previous PluginManager methods.

	Result<void, Error> discover(std::string const& directory);
	Result<void, Error> forget(std::string const& path);
	Result<void, Error> open(std::string const& path);
	Result<void, Error> close(std::string const& path);
	std::size_t         closeAllOpened();
	Result<void, Error> load(std::string const& path);
	Result<void, Error> unload(std::string const& path);

	// --- Aggregate ---------------------------------------------------------
	LoadSummary discoverAndLoad(std::string const& directory);

	// Dry-runs the requirement check that load() would perform against the
	// framework's internal ServiceManager.
	Result<void, Error> checkRequirements(Span<const ServiceRequirement> reqs);

	// --- Queries -----------------------------------------------------------
	std::vector<PluginInfo>   plugins();
	std::vector<PluginInfo>   plugins(State state);
	std::optional<PluginInfo> pluginInfo(std::string const& path);
	bool                      is(State state, std::string const& path);
	bool                      isDiscovered(std::string const& path);
	bool                      isOpened    (std::string const& path);
	bool                      isLoaded    (std::string const& path);

	// --- Garbage queue -----------------------------------------------------
	// Drains / queries the framework-owned deferred-dlclose queue.
	std::size_t collectGarbage() noexcept;
	std::size_t pendingGarbage() noexcept;

} // namespace thx::plugin
