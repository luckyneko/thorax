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
#include <memory>
#include <new>

namespace thx
{
	// Function pointer types for the legacy single-service plugin exports.
	// Will be removed in a future release; new plugins should use the
	// IPlugin-based exports below.
	using ServiceCreateFn  = IService*   (*)();
	using ServiceDestroyFn = void        (*)(IService*);
	using AbiVersionFn     = uint32_t    (*)();

	// Function pointer types for the IPlugin-based plugin exports.
	using PluginCreateFn   = IPlugin*    (*)();
	using PluginDestroyFn  = void        (*)(IPlugin*);

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

// On Windows, symbols must be explicitly marked for export from a DLL.
#if defined(_WIN32)
#  define THX_PLUGIN_EXPORT __declspec(dllexport)
#else
#  define THX_PLUGIN_EXPORT
#endif

// THX_DEFINE_PLUGIN(Type) emits the legacy single-service plugin exports:
//
//   extern "C" thx::IService* thx_create();
//   extern "C" void           thx_destroy(thx::IService*);
//   extern "C" uint32_t       thx_abi_version();
//
// Deprecated: prefer THX_DEFINE_SERVICE_PLUGIN (single-service) or
// THX_DEFINE_CUSTOM_PLUGIN (multi-service / custom IPlugin) below. The legacy
// macro will be removed in a future release once all in-tree plugins migrate.
//
// thx_abi_version() embeds the major component of THORAX_VERSION at plugin
// compile time. PluginHandle::open() checks this against the host's major
// version and rejects mismatches before any service is registered.
//
// The create function uses placement-new with std::nothrow so that allocation
// failure returns nullptr rather than throwing; ServiceManager already rejects
// a null result from the factory.
#define THX_DEFINE_PLUGIN(Type)                                       \
	extern "C" THX_PLUGIN_EXPORT thx::IService* thx_create()          \
	{                                                                  \
		return new (std::nothrow) Type();                              \
	}                                                                  \
	extern "C" THX_PLUGIN_EXPORT void thx_destroy(thx::IService* p)   \
	{                                                                  \
		delete p;                                                      \
	}                                                                  \
	extern "C" THX_PLUGIN_EXPORT uint32_t thx_abi_version()           \
	{                                                                  \
		return thx::THORAX_VERSION.major;                              \
	}

// THX_DEFINE_SERVICE_PLUGIN(ServiceType) — the common one-service-per-DSO
// shape. Emits an IPlugin shim that registers exactly one service of the
// given type in onLoad and unregisters it in onUnload.
//
// Place this macro once in a .cpp file. ServiceType must inherit from
// thx::Service<ServiceType>, define static_version(), and be default-constructible.
#define THX_DEFINE_SERVICE_PLUGIN(ServiceType)                                \
	extern "C" THX_PLUGIN_EXPORT thx::IPlugin* thx_create_plugin()             \
	{                                                                          \
		return new (std::nothrow) thx::ServicePluginShim<ServiceType>();       \
	}                                                                          \
	extern "C" THX_PLUGIN_EXPORT void thx_destroy_plugin(thx::IPlugin* p)      \
	{                                                                          \
		delete p;                                                              \
	}                                                                          \
	extern "C" THX_PLUGIN_EXPORT uint32_t thx_abi_version()                    \
	{                                                                          \
		return thx::THORAX_VERSION.major;                                      \
	}

// THX_DEFINE_CUSTOM_PLUGIN(PluginType) — power-user form. The plugin author
// supplies their own IPlugin subclass, free to register multiple services or
// declare required() dependencies.
//
// PluginType must inherit from thx::IPlugin and be default-constructible.
#define THX_DEFINE_CUSTOM_PLUGIN(PluginType)                                  \
	extern "C" THX_PLUGIN_EXPORT thx::IPlugin* thx_create_plugin()             \
	{                                                                          \
		return new (std::nothrow) PluginType();                                \
	}                                                                          \
	extern "C" THX_PLUGIN_EXPORT void thx_destroy_plugin(thx::IPlugin* p)      \
	{                                                                          \
		delete p;                                                              \
	}                                                                          \
	extern "C" THX_PLUGIN_EXPORT uint32_t thx_abi_version()                    \
	{                                                                          \
		return thx::THORAX_VERSION.major;                                      \
	}
