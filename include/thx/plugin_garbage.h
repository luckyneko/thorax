/*
 *  Created by LuckyNeko on 22/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include <cstddef>
#include <mutex>
#include <vector>

namespace thx
{
	// Process-wide deferred-dlclose queue for plugin DSOs.
	//
	// PluginHandle::close() pushes its native handle here instead of calling
	// dlclose/FreeLibrary immediately. The actual unmap is delayed until
	// collect() runs (PluginManager::open invokes it implicitly; callers may
	// also drive it via thx::collectPluginGarbage()).
	//
	// This indirection is what makes "hold a service across unload" safe: the
	// service's destructor and shared_ptr control block both live in plugin
	// code, so the DSO must remain mapped until every reference into it has
	// finished executing — which can't be detected synchronously from inside a
	// shared_ptr deleter.
	//
	// Thread-safe.
	class PluginGarbage
	{
	public:
		PluginGarbage()  = default;
		~PluginGarbage() = default;

		PluginGarbage(PluginGarbage const&)            = delete;
		PluginGarbage& operator=(PluginGarbage const&) = delete;

		// Process-wide singleton accessor. Forwards to the Registry-owned
		// PluginGarbage — Registry::instance() owns the actual queue, this
		// accessor is the legacy entry point.
		static PluginGarbage& instance() noexcept;

		// Queues a native DSO handle for deferred unmap. Null is a safe no-op.
		// The handle is opaque to this class — it's whatever PluginHandle stores
		// (void* / HMODULE), and the platform-specific unmap call happens inside
		// collect().
		void schedule(void* handle) noexcept;

		// Unmaps every queued DSO and clears the queue. Returns the number of
		// DSOs actually unmapped. Safe to call when the queue is empty.
		//
		// Safety: any shared_ptr<IService> registered by one of those plugins
		// MUST be released before calling. After collection, code belonging to
		// the unmapped DSO (including shared_ptr control-block destructors for
		// any leftover service refs) is no longer reachable.
		std::size_t collect() noexcept;

		// Number of DSOs awaiting unmap.
		std::size_t pending() const noexcept;

	private:
		mutable std::mutex  m_mutex;
		std::vector<void*>  m_handles;
	};

	// Free-function shims preserved for callers that don't want to reach for
	// PluginGarbage::instance() directly. Both forward to the singleton.
	std::size_t collectPluginGarbage() noexcept;
	std::size_t pendingPluginGarbage() noexcept;

} // namespace thx
