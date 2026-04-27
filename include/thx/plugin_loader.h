/*
 *  Created by LuckyNeko on 24/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "thx/iplugin.h"
#include "thx/plugin_handle.h"
#include "thx/result.h"
#include "thx/service_id.h"
#include "thx/service_manager.h"
#include "thx/log.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace thx
{
	// Snapshot entry returned by PluginLoader::list_plugins().
	struct LoadedPluginInfo
	{
		std::string path;
		ServiceID   service_id;
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
	// Destruction: any plugins still loaded when the PluginLoader is destroyed
	// are unloaded automatically (services unregistered, DSO handles deferred).
	// The destructor does NOT call collect_plugin_garbage(); call it explicitly
	// when no service references into those DSOs remain.
	class PluginLoader
	{
	public:
		explicit PluginLoader(ServiceManager& sm);
		~PluginLoader();

		PluginLoader(PluginLoader const&)            = delete;
		PluginLoader& operator=(PluginLoader const&) = delete;

		// Loads the plugin DSO at path and registers its service.
		// If the canonical path is already loaded, returns ok (no-op).
		//
		// As a side effect, drains the deferred-close queue (see unload). This
		// keeps the queue bounded in long-running programs but means any
		// service references held over from an earlier unload MUST be released
		// before calling load() — otherwise the drain unmaps the DSO out from
		// under them.
		Result<void, Error> load(std::string const& path);

		// Unregisters the plugin's services and releases the DSO from this loader.
		// Returns Err(NotLoaded) if path was not previously loaded.
		//
		// Lifetime: the DSO is NOT immediately unmapped. Its native handle is
		// pushed onto a process-wide deferred-close queue, drained at the next
		// call to load() or thx::collect_plugin_garbage(). This means callers
		// MAY hold shared_ptr<IService> handles across unload — the DSO stays
		// mapped (and the service's destructor / shared_ptr control block stay
		// reachable) until the next drain. Once collect_plugin_garbage() runs,
		// every still-held service reference into the unmapped DSO becomes
		// undefined behaviour, so drain only when no such references remain.
		Result<void, Error> unload(std::string const& path);

		// Returns true if the canonical path is currently loaded.
		bool is_loaded(std::string const& path) const;

		// Scans directory for files whose extension matches the platform plugin
		// extension (.dylib / .so / .dll). Does not load them.
		std::vector<std::string> discover(std::string const& directory) const;

		// Discovers all plugins in directory and loads each one.
		// Per-file errors are logged via thx::log() and skipped.
		// Returns ok unless no plugins were found or all failed to load.
		Result<void, Error> discover_and_load(std::string const& directory);

		// Returns a snapshot of currently loaded plugins and the service ID each
		// registered. Useful for diagnostics and test assertions.
		std::vector<LoadedPluginInfo> list_plugins() const;

	private:
		struct Entry
		{
			// Declaration order matters: `plugin` is destroyed before `handle`,
			// so the IPlugin's destructor (which lives in plugin code) runs
			// before the DSO is dlclose()d.
			PluginHandle             handle;
			std::vector<ServiceID>   service_ids;
			std::shared_ptr<IPlugin> plugin;
		};

		ServiceManager& sm_;
		std::unordered_map<std::string, Entry> plugins_; // canonical_path → entry

		static std::string resolve_canonical(std::string const& path);
	};

	// Unmaps every DSO that has been released via PluginLoader::unload (or
	// PluginLoader destruction) since the last drain. Returns the number of
	// DSOs actually unmapped.
	//
	// Safety: any shared_ptr<IService> that was registered by one of those
	// plugins MUST be released before calling this. After collection, code
	// belonging to the unmapped DSO (including the shared_ptr control block
	// destructors for any leftover service refs) is no longer reachable.
	std::size_t collect_plugin_garbage() noexcept;

	// Number of DSOs awaiting unmap. Useful for diagnostics and tests.
	std::size_t pending_plugin_garbage() noexcept;

} // namespace thx
