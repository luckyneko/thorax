/*
 *  Created by LuckyNeko on 24/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "library.h"
#include "thx/plugin/platform.h"
#include "thx/result.h"
#include "thx_internal_api.h"

#include <string>

// PluginHandle is the host-side *consumer* of the plugin ABI. The matching
// *producer* lives in thx/plugin/platform.h: the THX_DEFINE_SERVICE_PLUGIN
// and THX_DEFINE_PLUGIN macros emit the `thx_create_plugin` /
// `thx_destroy_plugin` / `thx_abi_version` symbols that PluginHandle::open()
// resolves here via Library::bind. See platform.h for the symbol definitions.

namespace thx::plugin
{
	// Plugin-specific layer on top of Library: holds an opened DSO plus the
	// three thx_* entry points resolved out of it. PluginManager owns the
	// lifecycle; framework users never construct a PluginHandle directly.
	//
	// Move-only. The destructor releases the underlying Library to the
	// process-wide PluginGarbage queue so that any IService references
	// returned by the plugin can outlive the unload call (see plugin_garbage.h
	// for the keep-alive contract). Failure paths in open() — symbol missing,
	// ABI mismatch — close the Library synchronously, since no plugin code
	// has had a chance to hand out references yet.
	class THX_INTERNAL_API PluginHandle
	{
	public:
		PluginHandle() = default;
		~PluginHandle();

		PluginHandle(PluginHandle&&) noexcept;
		PluginHandle& operator=(PluginHandle&&) noexcept;

		PluginHandle(const PluginHandle&) = delete;
		PluginHandle& operator=(const PluginHandle&) = delete;

		// True if the handle holds an open DSO.
		explicit operator bool() const noexcept { return static_cast<bool>(m_library); }

		const std::string& path() const noexcept { return m_library.path(); }

		PluginCreateFn createFn() const noexcept { return m_createFn; }
		PluginDestroyFn destroyFn() const noexcept { return m_destroyFn; }

		// Opens the DSO at path and resolves thx_create_plugin /
		// thx_destroy_plugin / thx_abi_version. Returns Err if the file is
		// missing, the ABI version is incompatible, or any symbol is absent.
		//
		// `flags` is forwarded to Library::open — pass LoadFlags::Strict for
		// fail-fast loading when a plugin has missing transitive deps. Default
		// is Lazy.
		static Result<PluginHandle, Error> open(
			const std::string& path,
			Library::LoadFlags flags = Library::LoadFlags::Lazy);

	private:
		void close() noexcept;

		Library m_library;
		PluginCreateFn m_createFn = nullptr;
		PluginDestroyFn m_destroyFn = nullptr;
	};

} // namespace thx::plugin