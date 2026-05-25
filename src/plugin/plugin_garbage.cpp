/*
 *  Created by LuckyNeko on 22/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "plugin/plugin_garbage.h"

#include <utility>

namespace thx::plugin
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

} // namespace thx::plugin