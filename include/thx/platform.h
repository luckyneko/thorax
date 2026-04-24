/*
 *  Created by LuckyNeko on 24/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "thx/iservice.h"

#include <memory>
#include <new>

namespace thx
{
	// Function pointer types for the two required plugin exports.
	using ServiceCreateFn  = IService* (*)();
	using ServiceDestroyFn = void (*)(IService*);

	// Platform plugin file extension, used by PluginLoader::discover().
#if defined(_WIN32)
	inline constexpr const char* kPluginExtension = ".dll";
#elif defined(__APPLE__)
	inline constexpr const char* kPluginExtension = ".dylib";
#else
	inline constexpr const char* kPluginExtension = ".so";
#endif

	// Wraps a raw IService pointer (produced by a plugin's thx_create export)
	// in a shared_ptr whose deleter calls the paired thx_destroy export from the
	// same DSO. This ensures allocate and free always happen on the same side of
	// the DSO boundary, preventing heap corruption from mismatched allocators.
	//
	// Returns nullptr if raw or destroy is null.
	inline std::shared_ptr<IService>
	make_service(IService* raw, ServiceDestroyFn destroy) noexcept
	{
		if (!raw || !destroy)
			return nullptr;
		return std::shared_ptr<IService>(raw, destroy);
	}

} // namespace thx

// ---------------------------------------------------------------------------
// Plugin-side export helpers
// ---------------------------------------------------------------------------

// THX_DEFINE_PLUGIN(Type) emits the two C-linkage symbols that every thorax
// plugin shared library must export:
//
//   extern "C" thx::IService* thx_create();
//   extern "C" void           thx_destroy(thx::IService*);
//
// Place this macro once in a .cpp file (not a header). It is a macro because
// extern "C" and fixed symbol names cannot be expressed in standard C++
// without one.
//
// The create function uses placement-new with std::nothrow so that allocation
// failure returns nullptr rather than throwing; ServiceManager already rejects
// a null result from the factory.
#define THX_DEFINE_PLUGIN(Type)                              \
	extern "C" thx::IService* thx_create()                   \
	{                                                        \
		return new (std::nothrow) Type();                    \
	}                                                        \
	extern "C" void thx_destroy(thx::IService* p)            \
	{                                                        \
		delete p;                                            \
	}
