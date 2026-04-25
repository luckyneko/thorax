/*
 *  Created by LuckyNeko on 24/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "thx/platform.h"
#include "thx/result.h"

#include <string>

namespace thx
{
	// RAII wrapper around a platform DSO (dynamic shared object) handle.
	//
	// Move-only. The destructor closes the DSO via dlclose / FreeLibrary.
	// Obtain via PluginHandle::open(); PluginLoader manages the lifecycle.
	class PluginHandle
	{
	public:
		PluginHandle() = default;
		~PluginHandle();

		PluginHandle(PluginHandle&&) noexcept;
		PluginHandle& operator=(PluginHandle&&) noexcept;

		PluginHandle(PluginHandle const&)            = delete;
		PluginHandle& operator=(PluginHandle const&) = delete;

		// True if the handle holds an open DSO.
		explicit operator bool() const noexcept { return handle_ != nullptr; }

		std::string const& path()       const noexcept { return path_;       }

		// Legacy single-service exports (thx_create / thx_destroy).
		// May be null if the DSO uses the IPlugin-based exports instead.
		ServiceCreateFn    create_fn()  const noexcept { return create_fn_;  }
		ServiceDestroyFn   destroy_fn() const noexcept { return destroy_fn_; }

		// IPlugin-based exports (thx_create_plugin / thx_destroy_plugin).
		// May be null if the DSO uses the legacy single-service exports instead.
		PluginCreateFn     plugin_create_fn()  const noexcept { return plugin_create_fn_;  }
		PluginDestroyFn    plugin_destroy_fn() const noexcept { return plugin_destroy_fn_; }

		// True if the DSO exports the IPlugin-based ABI; false if it uses the
		// legacy single-service ABI. Exactly one is guaranteed to be true after
		// a successful open().
		bool has_iplugin_abi() const noexcept { return plugin_create_fn_ != nullptr; }

		// Opens the DSO at path and resolves at least one of:
		//   - thx_create_plugin + thx_destroy_plugin (preferred, IPlugin-based)
		//   - thx_create + thx_destroy (legacy single-service)
		// Returns Err if the file is missing, the ABI version is incompatible,
		// or neither symbol pair is fully present.
		static Result<PluginHandle, Error> open(std::string const& path);

	private:
		void close() noexcept;

		void*            handle_            = nullptr;
		std::string      path_;
		ServiceCreateFn  create_fn_         = nullptr;
		ServiceDestroyFn destroy_fn_        = nullptr;
		PluginCreateFn   plugin_create_fn_  = nullptr;
		PluginDestroyFn  plugin_destroy_fn_ = nullptr;
	};

} // namespace thx
