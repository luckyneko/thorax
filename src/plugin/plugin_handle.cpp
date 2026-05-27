/*
 *  Created by LuckyNeko on 24/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "plugin/plugin_handle.h"
#include "registry.h"
#include "thx/to_string.h"
#include "thx/version.h"

#include <utility>

namespace thx::plugin
{

PluginHandle::~PluginHandle()
{
	close();
}

PluginHandle::PluginHandle(PluginHandle&& other) noexcept
	: m_library(std::move(other.m_library))
	, m_createFn(other.m_createFn)
	, m_destroyFn(other.m_destroyFn)
{
	other.m_createFn  = nullptr;
	other.m_destroyFn = nullptr;
}

PluginHandle& PluginHandle::operator=(PluginHandle&& other) noexcept
{
	if (this != &other)
	{
		close();
		m_library   = std::move(other.m_library);
		m_createFn  = other.m_createFn;
		m_destroyFn = other.m_destroyFn;
		other.m_createFn  = nullptr;
		other.m_destroyFn = nullptr;
	}
	return *this;
}

void PluginHandle::close() noexcept
{
	if (!m_library)
		return;
	// Hand the Library to PluginGarbage rather than unmapping immediately;
	// see plugin_garbage.h for the lifetime contract. The queue's destructor
	// (or an explicit collect()) is what eventually runs dlclose.
	Registry::instance().pluginGarbage().schedule(std::move(m_library));
	m_createFn  = nullptr;
	m_destroyFn = nullptr;
}

Result<PluginHandle, Error> PluginHandle::open(std::string const& path)
{
	Library lib;
	lib.open(path);
	if (!lib)
	{
		// Catch-all for dlopen / LoadLibrary failures: missing transitive
		// deps, permissions errors, malformed DSO, etc. The exact reason is
		// in the platform error string. We don't pre-check existence here —
		// PluginManager's resolveCanonical step is the layer that reports
		// FileNotFound for genuinely-missing paths.
		auto msg = lib.error();
		return Result<PluginHandle, Error>::err({ErrorCode::OpenFailed,
			msg.empty() ? path : msg});
	}

	PluginCreateFn  createFn  = nullptr;
	PluginDestroyFn destroyFn = nullptr;
	AbiVersionFn    abiVerFn  = nullptr;

	lib.bind("thx_create_plugin",  createFn)
	   .bind("thx_destroy_plugin", destroyFn)
	   .bind("thx_abi_version",    abiVerFn);

	if (!lib)
	{
		return Result<PluginHandle, Error>::err({ErrorCode::SymbolNotFound,
			"thx_create_plugin, thx_destroy_plugin, or thx_abi_version not found in: " + path});
	}

	{
		Version pluginV(abiVerFn());
		if (!Version::compatible(pluginV, THORAX_VERSION))
		{
			std::string msg = "Plugin built against thorax "
			    + toString(pluginV)
			    + " is not compatible with host "
			    + toString(THORAX_VERSION)
			    + ": " + path;
			// Synchronous close: no IPlugin instantiated yet, no live refs to
			// worry about. ~Library at end of scope runs dlclose immediately.
			return Result<PluginHandle, Error>::err({ErrorCode::VersionMismatch, std::move(msg)});
		}
	}

	PluginHandle handle;
	handle.m_library   = std::move(lib);
	handle.m_createFn  = createFn;
	handle.m_destroyFn = destroyFn;
	return Result<PluginHandle, Error>::ok(std::move(handle));
}

} // namespace thx::plugin