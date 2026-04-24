/*
 *  Created by LuckyNeko on 24/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "thx/plugin_handle.h"
#include "thx/version.h"

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

namespace thx
{

void PluginHandle::close() noexcept
{
	if (!handle_)
		return;
#if defined(_WIN32)
	FreeLibrary(static_cast<HMODULE>(handle_));
#else
	dlclose(handle_);
#endif
	handle_     = nullptr;
	create_fn_  = nullptr;
	destroy_fn_ = nullptr;
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
		handle_     = other.handle_;
		path_       = std::move(other.path_);
		create_fn_  = other.create_fn_;
		destroy_fn_ = other.destroy_fn_;
		other.handle_     = nullptr;
		other.create_fn_  = nullptr;
		other.destroy_fn_ = nullptr;
	}
	return *this;
}

Result<PluginHandle, Error> PluginHandle::open(std::string const& path)
{
#if defined(_WIN32)
	HMODULE h = LoadLibraryExA(path.c_str(), nullptr, 0);
	if (!h)
	{
		char buf[256] = {};
		FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		               nullptr, GetLastError(), 0, buf, sizeof(buf), nullptr);
		return Result<PluginHandle, Error>::err({ErrorCode::FileNotFound, buf});
	}

	auto create     = reinterpret_cast<ServiceCreateFn> (GetProcAddress(h, "thx_create"));
	auto destroy    = reinterpret_cast<ServiceDestroyFn>(GetProcAddress(h, "thx_destroy"));
	auto abi_ver_fn = reinterpret_cast<AbiVersionFn>    (GetProcAddress(h, "thx_abi_version"));

	if (!create || !destroy)
	{
		FreeLibrary(h);
		return Result<PluginHandle, Error>::err({ErrorCode::SymbolNotFound,
			"thx_create or thx_destroy not found in: " + path});
	}

	if (abi_ver_fn && abi_ver_fn() != thx::THORAX_VERSION.major)
	{
		FreeLibrary(h);
		return Result<PluginHandle, Error>::err({ErrorCode::VersionMismatch,
			"ABI major version mismatch in: " + path});
	}
#else
	void* h = dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
	if (!h)
	{
		const char* msg = dlerror();
		return Result<PluginHandle, Error>::err({ErrorCode::FileNotFound,
			msg ? msg : path});
	}

	// Casting void* to function pointer is implementation-defined but universally
	// supported and the only portable way to use dlsym in C++.
	auto create     = reinterpret_cast<ServiceCreateFn> (dlsym(h, "thx_create"));
	auto destroy    = reinterpret_cast<ServiceDestroyFn>(dlsym(h, "thx_destroy"));
	auto abi_ver_fn = reinterpret_cast<AbiVersionFn>    (dlsym(h, "thx_abi_version"));

	if (!create || !destroy)
	{
		dlclose(h);
		return Result<PluginHandle, Error>::err({ErrorCode::SymbolNotFound,
			"thx_create or thx_destroy not found in: " + path});
	}

	if (abi_ver_fn && abi_ver_fn() != thx::THORAX_VERSION.major)
	{
		dlclose(h);
		return Result<PluginHandle, Error>::err({ErrorCode::VersionMismatch,
			"ABI major version mismatch in: " + path});
	}
#endif

	PluginHandle handle;
	handle.handle_     = h;
	handle.path_       = path;
	handle.create_fn_  = create;
	handle.destroy_fn_ = destroy;
	return Result<PluginHandle, Error>::ok(std::move(handle));
}

} // namespace thx
