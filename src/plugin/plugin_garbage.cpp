/*
 *  Created by LuckyNeko on 22/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "thx/plugin/plugin_garbage.h"
#include "thx/registry.h"

#include <utility>

namespace thx
{

void PluginGarbage::schedule(Library lib) noexcept
{
	if (!lib)
		return;
	std::lock_guard<std::mutex> lock(m_mutex);
	m_libraries.push_back(std::move(lib));
}

std::size_t PluginGarbage::collect() noexcept
{
	// Move the queued libraries out under the lock, then let their destructors
	// run after we release it: ~Library calls dlclose, which may execute
	// plugin code that re-enters this queue. Holding the lock during that
	// would deadlock.
	std::vector<Library> drained;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		drained.swap(m_libraries);
	}
	auto count = drained.size();
	// drained goes out of scope here — ~Library on each entry closes the DSO.
	return count;
}

std::size_t PluginGarbage::pending() const noexcept
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_libraries.size();
}

std::size_t collectPluginGarbage() noexcept
{
	return Registry::instance().pluginGarbage().collect();
}

std::size_t pendingPluginGarbage() noexcept
{
	return Registry::instance().pluginGarbage().pending();
}

} // namespace thx
