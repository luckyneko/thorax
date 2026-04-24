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
		ServiceCreateFn    create_fn()  const noexcept { return create_fn_;  }
		ServiceDestroyFn   destroy_fn() const noexcept { return destroy_fn_; }

		// Opens the DSO at path and resolves thx_create / thx_destroy.
		// Returns Err if the file is missing or either symbol is absent.
		static Result<PluginHandle, Error> open(std::string const& path);

	private:
		void close() noexcept;

		void*            handle_     = nullptr;
		std::string      path_;
		ServiceCreateFn  create_fn_  = nullptr;
		ServiceDestroyFn destroy_fn_ = nullptr;
	};

} // namespace thx
