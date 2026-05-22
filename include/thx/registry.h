/*
 *  Created by LuckyNeko on 22/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "thx/plugin_garbage.h"
#include "thx/service_manager.h"

#include <string>

namespace thx
{
	// Top-level container that owns the framework's process-wide state.
	//
	// Today Registry owns:
	//   - the ServiceManager (process-wide service registry);
	//   - the PluginGarbage queue (deferred-dlclose queue for plugin DSOs).
	//
	// Both members are accessible by reference and have stable addresses across
	// the lifetime of the singleton — callers may take and hold references
	// freely. The class is intentionally non-copyable / non-movable.
	//
	// PluginManager is *not* owned by Registry yet. It still takes a
	// ServiceManager& in its constructor and is constructed by the host. A
	// future revision is expected to add Registry::pluginManager() and move
	// that ownership in.
	class Registry
	{
	public:
		Registry(Registry const&)            = delete;
		Registry& operator=(Registry const&) = delete;
		Registry(Registry&&)                 = delete;
		Registry& operator=(Registry&&)      = delete;

		// Process-wide singleton accessor. Constructs lazily on first call.
		static Registry& instance() noexcept;

		ServiceManager& serviceManager() noexcept { return m_serviceManager; }
		PluginGarbage&  pluginGarbage()  noexcept { return m_pluginGarbage;  }

		// Optional human-readable name set via thx::initialise(). Used for
		// diagnostics; has no effect on framework behaviour. Empty until
		// initialise() is called.
		std::string const& debugName() const noexcept { return m_debugName; }

	private:
		Registry() = default;

		// Allow the free-function lifecycle hooks to mutate state without
		// exposing it on the public surface.
		friend bool initialise(std::string debugName);
		friend void shutdown() noexcept;

		// Declaration order matters: m_pluginGarbage is declared first so that
		// it is destroyed last. Anything else (notably future PluginManager
		// ownership) that schedules handles into the garbage queue at
		// destruction will then still find a live queue to push into.
		PluginGarbage   m_pluginGarbage;
		ServiceManager  m_serviceManager;
		std::string     m_debugName;
	};

	// Free-function lifecycle for the Registry singleton.
	//
	// initialise() records an optional human-readable name on the Registry.
	// The Registry itself is lazily constructed by Registry::instance() on
	// first use — calling initialise() is not required to use the framework.
	//
	// Returns true if this call set the debug name (i.e. it was empty before),
	// false if a previous initialise() already set one.
	bool initialise(std::string debugName = "thorax");

	// shutdown() drains the deferred-close queue. It does NOT destroy the
	// Registry — the singleton persists until program exit. Safe to call
	// multiple times; equivalent to thx::collectPluginGarbage() followed by
	// clearing the debug name.
	//
	// Safety: callers MUST release any shared_ptr<IService> references into
	// unloaded DSOs before calling shutdown(). See plugin_garbage.h.
	void shutdown() noexcept;

	// Shorthand for Registry::instance(). Exists so callers don't have to type
	// the class name; behaves identically.
	Registry& registry() noexcept;

} // namespace thx
