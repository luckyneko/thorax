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
	// Obtain via PluginHandle::open(); PluginManager manages the lifecycle.
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

		PluginCreateFn     create_fn()  const noexcept { return create_fn_;  }
		PluginDestroyFn    destroy_fn() const noexcept { return destroy_fn_; }

		// Opens the DSO at path and resolves thx_create_plugin / thx_destroy_plugin.
		// Returns Err if the file is missing, the ABI version is incompatible, or
		// either symbol is absent.
		static Result<PluginHandle, Error> open(std::string const& path);

	private:
		void close() noexcept;

		void*           handle_     = nullptr;
		std::string     path_;
		PluginCreateFn  create_fn_  = nullptr;
		PluginDestroyFn destroy_fn_ = nullptr;
	};

} // namespace thx
