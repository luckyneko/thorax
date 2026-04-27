/*
 *  Created by LuckyNeko on 24/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "thx/iplugin.h"
#include "thx/iservice.h"
#include "thx/version.h"

#include <cstdint>
#include <new>

namespace thx
{
	// Function pointer types for the plugin C exports.
	using PluginCreateFn   = IPlugin*    (*)();
	using PluginDestroyFn  = void        (*)(IPlugin*);
	using AbiVersionFn     = uint32_t    (*)();

	// Platform plugin file extension, used by PluginLoader::discover().
#if defined(_WIN32)
	inline constexpr const char* kPluginExtension = ".dll";
#elif defined(__APPLE__)
	inline constexpr const char* kPluginExtension = ".dylib";
#else
	inline constexpr const char* kPluginExtension = ".so";
#endif

} // namespace thx

// ---------------------------------------------------------------------------
// Plugin-side export helpers
// ---------------------------------------------------------------------------

// THX_PLUGIN_API bundles extern "C", the platform DLL-export attribute, and
// (on POSIX with -fvisibility=hidden) an explicit default-visibility marker
// into a single decorator for the three thx_* plugin entry-points.
//
// Using -fvisibility=hidden on plugin DSOs ensures that only these three
// symbols are visible to the dynamic linker, avoiding ODR collisions between
// independently loaded plugins that happen to define the same internal names.
#if defined(_WIN32)
#  define THX_PLUGIN_API extern "C" __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#  define THX_PLUGIN_API extern "C" __attribute__((visibility("default")))
#else
#  define THX_PLUGIN_API extern "C"
#endif

// Every thorax plugin shared library must export three C-linkage symbols:
//
//   thx::IPlugin* thx_create_plugin();
//   void          thx_destroy_plugin(thx::IPlugin*);
//   uint32_t      thx_abi_version();
//
// Use one of the macros below to emit them; they are macros because fixed
// symbol names and extern "C" cannot be expressed in standard C++ without one.
//
// thx_abi_version() embeds the major component of THORAX_VERSION at plugin
// compile time. PluginHandle::open() checks this against the host's major
// version and rejects mismatches before the plugin is instantiated.
//
// The create function uses placement-new with std::nothrow so allocation
// failure returns nullptr rather than throwing; PluginLoader already rejects
// a null result.

// THX_DEFINE_SERVICE_PLUGIN(ServiceType) — the common one-service-per-DSO
// shape. Emits an IPlugin shim that registers exactly one service of the
// given type in onLoad and unregisters it in onUnload.
//
// Place this macro once in a .cpp file. ServiceType must inherit from
// thx::Service<ServiceType>, define static_version(), and be default-constructible.
#define THX_DEFINE_SERVICE_PLUGIN(ServiceType)                               \
	THX_PLUGIN_API thx::IPlugin* thx_create_plugin()                          \
	{                                                                         \
		return new (std::nothrow) thx::ServicePluginShim<ServiceType>();      \
	}                                                                         \
	THX_PLUGIN_API void thx_destroy_plugin(thx::IPlugin* p)                   \
	{                                                                         \
		delete p;                                                             \
	}                                                                         \
	THX_PLUGIN_API uint32_t thx_abi_version()                                 \
	{                                                                         \
		return thx::THORAX_VERSION.major;                                     \
	}

// THX_DEFINE_PLUGIN(PluginType) — power-user form. The plugin author supplies
// their own IPlugin subclass, free to register multiple services or declare
// required() dependencies.
//
// PluginType must inherit from thx::IPlugin and be default-constructible.
#define THX_DEFINE_PLUGIN(PluginType)                                        \
	THX_PLUGIN_API thx::IPlugin* thx_create_plugin()                          \
	{                                                                         \
		return new (std::nothrow) PluginType();                               \
	}                                                                         \
	THX_PLUGIN_API void thx_destroy_plugin(thx::IPlugin* p)                   \
	{                                                                         \
		delete p;                                                             \
	}                                                                         \
	THX_PLUGIN_API uint32_t thx_abi_version()                                 \
	{                                                                         \
		return thx::THORAX_VERSION.major;                                     \
	}
