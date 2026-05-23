/*
 *  Created by LuckyNeko on 24/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "thx/plugin/iplugin.h"
#include "thx/service/iservice.h"
#include "thx/version.h"

#include <cstdint>
#include <new>

// This header is the plugin-side *producer* of the plugin ABI: the
// THX_DEFINE_SERVICE_PLUGIN / THX_DEFINE_PLUGIN macros emit the three
// `thx_*` C symbols every plugin shared library must export. The host-side
// *consumer* lives in thx/plugin/plugin_handle.h, where PluginHandle::open()
// resolves the symbols via Library::bind. See plugin_handle.h for how these
// exports are consumed.

namespace thx::plugin
{
	// Function pointer types for the plugin C exports.
	using PluginCreateFn   = IPlugin*    (*)();
	using PluginDestroyFn  = void        (*)(IPlugin*);
	using AbiVersionFn     = uint32_t    (*)();

	// LIBRARY_EXTENSION moved to thx/library.h.

} // namespace thx::plugin
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
//   thx::plugin::IPlugin* thx_create_plugin();
//   void                  thx_destroy_plugin(thx::plugin::IPlugin*);
//   uint32_t              thx_abi_version();
//
// Use one of the macros below to emit them; they are macros because fixed
// symbol names and extern "C" cannot be expressed in standard C++ without one.
//
// thx_abi_version() returns THORAX_VERSION packed into a uint32_t (see
// Version::pack()). PluginHandle::open() unpacks it (Version(uint32_t)) and
// asks compatible():
// the plugin's major must match the host's, and the host's full
// major.minor.patch must be ≥ the plugin's. A plugin built against a newer
// thorax than the host is rejected; a plugin built against the same major
// but older minor/patch is accepted.
//
// The create function uses placement-new with std::nothrow so allocation
// failure returns nullptr rather than throwing; PluginManager already rejects
// a null result.

// THX_DEFINE_SERVICE_PLUGIN(ServiceType) — the common one-service-per-DSO
// shape. Emits an IPlugin shim that registers exactly one service of the
// given type in onLoad and unregisters it in onUnload.
//
// Place this macro once in a .cpp file. ServiceType must inherit from
// thx::service::Service<ServiceType>, define staticVersion(), and be default-constructible.
#define THX_DEFINE_SERVICE_PLUGIN(ServiceType)                                          \
	THX_PLUGIN_API thx::plugin::IPlugin* thx_create_plugin()                            \
	{                                                                                   \
		return new (std::nothrow) thx::plugin::ServicePluginShim<ServiceType>();        \
	}                                                                                   \
	THX_PLUGIN_API void thx_destroy_plugin(thx::plugin::IPlugin* p)                     \
	{                                                                                   \
		delete p;                                                                       \
	}                                                                                   \
	THX_PLUGIN_API uint32_t thx_abi_version()                                           \
	{                                                                                   \
		return thx::THORAX_VERSION.pack();                                              \
	}

// THX_DEFINE_PLUGIN(PluginType) — power-user form. The plugin author supplies
// their own IPlugin subclass, free to register multiple services or declare
// required() dependencies.
//
// PluginType must inherit from thx::plugin::IPlugin and be default-constructible.
#define THX_DEFINE_PLUGIN(PluginType)                                                   \
	THX_PLUGIN_API thx::plugin::IPlugin* thx_create_plugin()                            \
	{                                                                                   \
		return new (std::nothrow) PluginType();                                         \
	}                                                                                   \
	THX_PLUGIN_API void thx_destroy_plugin(thx::plugin::IPlugin* p)                     \
	{                                                                                   \
		delete p;                                                                       \
	}                                                                                   \
	THX_PLUGIN_API uint32_t thx_abi_version()                                           \
	{                                                                                   \
		return thx::THORAX_VERSION.pack();                                              \
	}
