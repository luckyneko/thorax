/*
 *  Created by LuckyNeko on 24/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "thx/plugin_handle.h"
#include "thx/registry.h"
#include "thx/to_string.h"
#include "thx/version.h"

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

#if defined(_WIN32)
	using NativeHandle = HMODULE;
	NativeHandle nativeOpen(const char* path)  { return LoadLibraryExA(path, nullptr, 0); }
	void         nativeClose(NativeHandle h)   { FreeLibrary(h); }
	void*        nativeSym(NativeHandle h, const char* n)
	                                            { return reinterpret_cast<void*>(GetProcAddress(h, n)); }
	std::string  nativeError(NativeHandle)
	{
		char buf[256] = {};
		FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		               nullptr, GetLastError(), 0, buf, static_cast<DWORD>(sizeof(buf)), nullptr);
		return buf;
	}
#else
	using NativeHandle = void*;
	NativeHandle nativeOpen(const char* path)  { return dlopen(path, RTLD_LAZY | RTLD_LOCAL); }
	void         nativeClose(NativeHandle h)   { dlclose(h); }
	void*        nativeSym(NativeHandle h, const char* n)
	                                            { return dlsym(h, n); }
	std::string  nativeError(NativeHandle)
	{
		const char* msg = dlerror();
		return msg ? std::string(msg) : std::string{};
	}
#endif

} // namespace

void PluginHandle::close() noexcept
{
	if (!m_handle)
		return;
	// Hand the native handle to PluginGarbage rather than unmapping immediately;
	// see plugin_garbage.h for the lifetime contract.
	Registry::instance().pluginGarbage().schedule(m_handle);
	m_handle      = nullptr;
	m_createFn   = nullptr;
	m_destroyFn  = nullptr;
}

PluginHandle::~PluginHandle()
{
	close();
}

PluginHandle::PluginHandle(PluginHandle&& other) noexcept
	: m_handle(other.m_handle)
	, m_path(std::move(other.m_path))
	, m_createFn(other.m_createFn)
	, m_destroyFn(other.m_destroyFn)
{
	other.m_handle     = nullptr;
	other.m_createFn  = nullptr;
	other.m_destroyFn = nullptr;
}

PluginHandle& PluginHandle::operator=(PluginHandle&& other) noexcept
{
	if (this != &other)
	{
		close();
		m_handle      = other.m_handle;
		m_path        = std::move(other.m_path);
		m_createFn   = other.m_createFn;
		m_destroyFn  = other.m_destroyFn;
		other.m_handle     = nullptr;
		other.m_createFn  = nullptr;
		other.m_destroyFn = nullptr;
	}
	return *this;
}

Result<PluginHandle, Error> PluginHandle::open(std::string const& path)
{
	NativeHandle h = nativeOpen(path.c_str());
	if (!h)
	{
		auto msg = nativeError(h);
		return Result<PluginHandle, Error>::err({ErrorCode::FileNotFound,
			msg.empty() ? path : msg});
	}

	// Casting void* to function pointer is implementation-defined but universally
	// supported and the only portable way to use dlsym in C++.
	auto createV   = reinterpret_cast<PluginCreateFn> (nativeSym(h, "thx_create_plugin"));
	auto destroyV  = reinterpret_cast<PluginDestroyFn>(nativeSym(h, "thx_destroy_plugin"));
	auto abiVerFn = reinterpret_cast<AbiVersionFn>   (nativeSym(h, "thx_abi_version"));

	if (!createV || !destroyV || !abiVerFn)
	{
		nativeClose(h);
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
			nativeClose(h);
			return Result<PluginHandle, Error>::err({ErrorCode::VersionMismatch, std::move(msg)});
		}
	}

	PluginHandle handle;
	handle.m_handle     = h;
	handle.m_path       = path;
	handle.m_createFn  = createV;
	handle.m_destroyFn = destroyV;
	return Result<PluginHandle, Error>::ok(std::move(handle));
}

} // namespace thx
