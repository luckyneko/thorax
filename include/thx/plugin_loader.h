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
#include <optional>
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
	// plugin registers exactly one service (via THX_DEFINE_PLUGIN). The loader
	// tracks canonical paths so loading the same file twice is a safe no-op.
	//
	// Thread safety: not thread-safe. Protect concurrent calls externally if needed.
	//
	// Destruction: any plugins still loaded when the PluginLoader is destroyed are
	// unloaded automatically (services unregistered, DSOs closed).
	class PluginLoader
	{
	public:
		explicit PluginLoader(ServiceManager& sm);
		~PluginLoader();

		PluginLoader(PluginLoader const&)            = delete;
		PluginLoader& operator=(PluginLoader const&) = delete;

		// Loads the plugin DSO at path and registers its service.
		// If the canonical path is already loaded, returns ok (no-op).
		Result<void, Error> load(std::string const& path);

		// Unregisters the plugin's service and closes the DSO.
		// Returns Err(NotLoaded) if path was not previously loaded.
		//
		// Safety: all shared_ptr<IService> handles to the service should be
		// released before calling unload; their deleters point into the DSO.
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
			// Declaration order matters: `plugin` is destroyed before `handle`
			// so that the IPlugin's destructor (which lives in the DSO) runs
			// before the DSO is dlclose()d.
			PluginHandle             handle;
			std::optional<ServiceID> legacy_service_id;
			std::shared_ptr<IPlugin> plugin;
		};

		ServiceManager& sm_;
		std::unordered_map<std::string, Entry> plugins_; // canonical_path → entry

		static std::string resolve_canonical(std::string const& path);
	};

} // namespace thx
