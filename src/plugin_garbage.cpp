/*
 *  Created by LuckyNeko on 22/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "thx/plugin_garbage.h"

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

namespace thx
{

namespace
{
	void nativeClose(void* handle) noexcept
	{
		if (!handle)
			return;
#if defined(_WIN32)
		FreeLibrary(static_cast<HMODULE>(handle));
#else
		dlclose(handle);
#endif
	}
} // namespace

PluginGarbage& PluginGarbage::instance() noexcept
{
	static PluginGarbage g;
	return g;
}

void PluginGarbage::schedule(void* handle) noexcept
{
	if (!handle)
		return;
	std::lock_guard<std::mutex> lock(m_mutex);
	m_handles.push_back(handle);
}

std::size_t PluginGarbage::collect() noexcept
{
	// Move the queued handles out under the lock, then unmap without holding
	// it: dlclose can run plugin destructors which may dlopen/dlclose other
	// libraries — keeping the lock would be a deadlock waiting to happen.
	std::vector<void*> pending;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		pending.swap(m_handles);
	}
	for (auto h : pending)
		nativeClose(h);
	return pending.size();
}

std::size_t PluginGarbage::pending() const noexcept
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_handles.size();
}

std::size_t collectPluginGarbage() noexcept
{
	return PluginGarbage::instance().collect();
}

std::size_t pendingPluginGarbage() noexcept
{
	return PluginGarbage::instance().pending();
}

} // namespace thx
