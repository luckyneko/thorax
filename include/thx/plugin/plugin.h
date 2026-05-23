/*
 *  Created by LuckyNeko on 22/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "thx/registry.h"
#include "thx/plugin/plugin_garbage.h"
#include "thx/plugin/plugin_manager.h"
#include "thx/result.h"
#include "thx/span.h"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Free-function shims over the Registry-owned PluginManager. Each function
// is a one-liner forwarding to `thx::registry().pluginManager().method(...)`,
// matching the method name exactly. Tests and the example host show how to
// use them; library internals that already hold a PluginManager& (e.g.
// PluginManager::discoverAndLoad iterating discover()) keep using the member
// functions directly.

namespace thx::plugin
{
	// discover → open → load (three-step flow).
	inline Result<void, Error> discover(std::string const& directory)
	{
		return thx::registry().pluginManager().discover(directory);
	}

	inline Result<void, Error> forget(std::string const& path)
	{
		return thx::registry().pluginManager().forget(path);
	}

	inline Result<void, Error> open(std::string const& path)
	{
		return thx::registry().pluginManager().open(path);
	}

	inline Result<void, Error> close(std::string const& path)
	{
		return thx::registry().pluginManager().close(path);
	}

	inline std::size_t closeAllOpened()
	{
		return thx::registry().pluginManager().closeAllOpened();
	}

	inline Result<void, Error> load(std::string const& path)
	{
		return thx::registry().pluginManager().load(path);
	}

	inline Result<void, Error> unload(std::string const& path)
	{
		return thx::registry().pluginManager().unload(path);
	}

	// Aggregate / convenience operations.
	inline PluginManager::LoadSummary discoverAndLoad(std::string const& directory)
	{
		return thx::registry().pluginManager().discoverAndLoad(directory);
	}

	// Query API — value-typed snapshots of the Registry-owned PluginManager.
	inline std::vector<PluginInfo> plugins()
	{
		return thx::registry().pluginManager().plugins();
	}

	inline std::vector<PluginInfo> plugins(State state)
	{
		return thx::registry().pluginManager().plugins(state);
	}

	inline std::optional<PluginInfo> pluginInfo(std::string const& path)
	{
		return thx::registry().pluginManager().pluginInfo(path);
	}

	inline bool is(State state, std::string const& path)
	{
		return thx::registry().pluginManager().is(state, path);
	}

	inline bool isDiscovered(std::string const& path)
	{
		return thx::registry().pluginManager().isDiscovered(path);
	}

	inline bool isOpened(std::string const& path)
	{
		return thx::registry().pluginManager().isOpened(path);
	}

	inline bool isLoaded(std::string const& path)
	{
		return thx::registry().pluginManager().isLoaded(path);
	}

	// Dry-run requirement check against the Registry-owned ServiceManager.
	// Equivalent to PluginManager::checkRequirements(registry().serviceManager(), reqs).
	inline Result<void, Error> checkRequirements(Span<const ServiceRequirement> reqs)
	{
		return PluginManager::checkRequirements(
		    thx::registry().serviceManager(), reqs);
	}

	// PluginGarbage convenience shims; operate on the Registry-owned queue.
	// Equivalent to thx::registry().pluginGarbage().collect() / .pending().
	inline std::size_t collectGarbage() noexcept
	{
		return thx::registry().pluginGarbage().collect();
	}

	inline std::size_t pendingGarbage() noexcept
	{
		return thx::registry().pluginGarbage().pending();
	}

} // namespace thx::plugin
