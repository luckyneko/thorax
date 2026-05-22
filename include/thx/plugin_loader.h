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
		std::string              path;
		std::string              plugin_name; // from IPlugin::name()
		std::vector<ServiceID>   services;    // all service IDs registered by this plugin
	};

	// A plugin DSO that has been opened and its IPlugin instantiated, but whose
	// onLoad has NOT yet been called and whose required() services have NOT yet
	// been checked. The caller queries name()/version()/required() to plan load
	// order, then passes the value to PluginLoader::load(OpenedPlugin) to
	// commit. Three-step flow: discover -> open -> load.
	//
	// Move-only. A default-constructed or moved-from OpenedPlugin is empty and
	// converts to false.
	//
	// Lifetime: while alive, the DSO is mapped and the IPlugin instance exists.
	// Dropping the value without passing it to load() destroys the IPlugin and
	// queues the DSO into the deferred-close graveyard (drained at the next
	// PluginLoader::open() or thx::collect_plugin_garbage()).
	class OpenedPlugin
	{
	public:
		OpenedPlugin() = default;
		~OpenedPlugin();

		OpenedPlugin(OpenedPlugin&&) noexcept;
		OpenedPlugin& operator=(OpenedPlugin&&) noexcept;

		OpenedPlugin(OpenedPlugin const&)            = delete;
		OpenedPlugin& operator=(OpenedPlugin const&) = delete;

		explicit operator bool() const noexcept { return static_cast<bool>(plugin_); }

		// Canonical filesystem path of the DSO.
		std::string const& path() const noexcept { return canonical_; }

		// Plugin-reported metadata. Valid once open() has succeeded.
		StringView                     name() const noexcept;
		Version                        version() const noexcept;
		Span<const ServiceRequirement> required() const noexcept;

	private:
		friend class PluginLoader;
		OpenedPlugin(PluginHandle handle,
		             std::shared_ptr<IPlugin> plugin,
		             std::string canonical);

		// Destruction order matters: plugin_ (whose destructor lives in plugin
		// code) is reset before handle_ is destroyed.
		PluginHandle             handle_;
		std::shared_ptr<IPlugin> plugin_;
		std::string              canonical_;
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
		// Returns Err(AlreadyLoaded) if this PluginLoader already has the
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
		static Result<void, Error> check_requirements(ServiceManager const&            sm,
		                                              Span<const ServiceRequirement>   reqs);

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

		// Outcome of a discover_and_load call: which paths loaded successfully
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
		LoadSummary discover_and_load(std::string const& directory);

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
