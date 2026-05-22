/*
 *  Created by LuckyNeko on 24/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "thx/plugin_handle.h"
#include "thx/plugin_garbage.h"
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
	NativeHandle native_open(const char* path)  { return LoadLibraryExA(path, nullptr, 0); }
	void         native_close(NativeHandle h)   { FreeLibrary(h); }
	void*        native_sym(NativeHandle h, const char* n)
	                                            { return reinterpret_cast<void*>(GetProcAddress(h, n)); }
	std::string  native_error(NativeHandle)
	{
		char buf[256] = {};
		FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		               nullptr, GetLastError(), 0, buf, static_cast<DWORD>(sizeof(buf)), nullptr);
		return buf;
	}
#else
	using NativeHandle = void*;
	NativeHandle native_open(const char* path)  { return dlopen(path, RTLD_LAZY | RTLD_LOCAL); }
	void         native_close(NativeHandle h)   { dlclose(h); }
	void*        native_sym(NativeHandle h, const char* n)
	                                            { return dlsym(h, n); }
	std::string  native_error(NativeHandle)
	{
		const char* msg = dlerror();
		return msg ? std::string(msg) : std::string{};
	}
#endif

} // namespace

void PluginHandle::close() noexcept
{
	if (!handle_)
		return;
	// Hand the native handle to PluginGarbage rather than unmapping immediately;
	// see plugin_garbage.h for the lifetime contract.
	PluginGarbage::instance().schedule(handle_);
	handle_      = nullptr;
	create_fn_   = nullptr;
	destroy_fn_  = nullptr;
}

PluginHandle::~PluginHandle()
{
	close();
}

PluginHandle::PluginHandle(PluginHandle&& other) noexcept
	: handle_(other.handle_)
	, path_(std::move(other.path_))
	, create_fn_(other.create_fn_)
	, destroy_fn_(other.destroy_fn_)
{
	other.handle_     = nullptr;
	other.create_fn_  = nullptr;
	other.destroy_fn_ = nullptr;
}

PluginHandle& PluginHandle::operator=(PluginHandle&& other) noexcept
{
	if (this != &other)
	{
		close();
		handle_      = other.handle_;
		path_        = std::move(other.path_);
		create_fn_   = other.create_fn_;
		destroy_fn_  = other.destroy_fn_;
		other.handle_     = nullptr;
		other.create_fn_  = nullptr;
		other.destroy_fn_ = nullptr;
	}
	return *this;
}

Result<PluginHandle, Error> PluginHandle::open(std::string const& path)
{
	NativeHandle h = native_open(path.c_str());
	if (!h)
	{
		auto msg = native_error(h);
		return Result<PluginHandle, Error>::err({ErrorCode::FileNotFound,
			msg.empty() ? path : msg});
	}

	// Casting void* to function pointer is implementation-defined but universally
	// supported and the only portable way to use dlsym in C++.
	auto create_v   = reinterpret_cast<PluginCreateFn> (native_sym(h, "thx_create_plugin"));
	auto destroy_v  = reinterpret_cast<PluginDestroyFn>(native_sym(h, "thx_destroy_plugin"));
	auto abi_ver_fn = reinterpret_cast<AbiVersionFn>   (native_sym(h, "thx_abi_version"));

	if (!create_v || !destroy_v || !abi_ver_fn)
	{
		native_close(h);
		return Result<PluginHandle, Error>::err({ErrorCode::SymbolNotFound,
			"thx_create_plugin, thx_destroy_plugin, or thx_abi_version not found in: " + path});
	}

	{
		Version plugin_v(abi_ver_fn());
		if (!Version::compatible(plugin_v, THORAX_VERSION))
		{
			std::string msg = "Plugin built against thorax "
			    + to_string(plugin_v)
			    + " is not compatible with host "
			    + to_string(THORAX_VERSION)
			    + ": " + path;
			native_close(h);
			return Result<PluginHandle, Error>::err({ErrorCode::VersionMismatch, std::move(msg)});
		}
	}

	PluginHandle handle;
	handle.handle_     = h;
	handle.path_       = path;
	handle.create_fn_  = create_v;
	handle.destroy_fn_ = destroy_v;
	return Result<PluginHandle, Error>::ok(std::move(handle));
}

} // namespace thx
