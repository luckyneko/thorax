/*
 *  Created by LuckyNeko on 22/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "thx/library.h"

#include <cstddef>
#include <mutex>
#include <vector>

namespace thx::plugin
{
	// Process-wide deferred-dlclose queue for plugin DSOs.
	//
	// PluginHandle::close() moves its Library here instead of letting the
	// destructor run dlclose/FreeLibrary immediately. The actual unmap is
	// delayed until collect() runs (PluginManager::open invokes it
	// implicitly; callers may also drive it via thx::plugin::collectGarbage()).
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

		// Queues a Library for deferred close. An empty Library is a safe
		// no-op. The Library is moved into the queue; on collect(), its
		// destructor runs dlclose / FreeLibrary.
		void schedule(Library lib) noexcept;

		// Closes every queued DSO and clears the queue. Returns the number
		// of DSOs unmapped. Safe to call when the queue is empty.
		//
		// Safety: any shared_ptr<IService> registered by one of those plugins
		// MUST be released before calling. After collection, code belonging to
		// the unmapped DSO (including shared_ptr control-block destructors for
		// any leftover service refs) is no longer reachable.
		std::size_t collect() noexcept;

		// Number of DSOs awaiting unmap.
		std::size_t pending() const noexcept;

	private:
		mutable std::mutex   m_mutex;
		std::vector<Library> m_libraries;
	};

} // namespace thx::plugin