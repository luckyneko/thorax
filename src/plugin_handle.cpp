/*
 *  Created by LuckyNeko on 24/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "thx/plugin_handle.h"
#include "thx/version.h"

#include <mutex>
#include <sstream>
#include <vector>

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

// Process-wide deferred-dlclose graveyard.
//
// PluginHandle::close() pushes its native handle here instead of calling
// dlclose immediately. The actual unmap is delayed until detail::drain() is
// called (PluginLoader::load runs it on entry; thx::collect_plugin_garbage
// is the public trigger).
//
// This indirection is what makes "hold a service across unload" safe: the
// service's destructor and shared_ptr control block both live in plugin
// code, so the DSO must remain mapped until every reference into it has
// finished executing — which can't be detected synchronously from inside a
// shared_ptr deleter.
struct DsoGraveyard
{
	std::mutex                mutex;
	std::vector<NativeHandle> handles;
};

DsoGraveyard& graveyard()
{
	static DsoGraveyard g;
	return g;
}

void schedule_close(NativeHandle h) noexcept
{
	if (!h)
		return;
	auto& g = graveyard();
	std::lock_guard<std::mutex> lock(g.mutex);
	g.handles.push_back(h);
}

} // namespace

namespace detail
{
	// Internal entry point used by PluginLoader; equivalent to the public
	// thx::collect_plugin_garbage().
	std::size_t drain_dso_graveyard() noexcept
	{
		// Move the queued handles out under the lock, then unmap without holding
		// it: dlclose can run plugin destructors which may dlopen/dlclose other
		// libraries — keeping the lock would be a deadlock waiting to happen.
		std::vector<NativeHandle> pending;
		{
			auto& g = graveyard();
			std::lock_guard<std::mutex> lock(g.mutex);
			pending.swap(g.handles);
		}
		for (auto h : pending)
			native_close(h);
		return pending.size();
	}

	std::size_t pending_dso_graveyard() noexcept
	{
		auto& g = graveyard();
		std::lock_guard<std::mutex> lock(g.mutex);
		return g.handles.size();
	}
} // namespace detail

void PluginHandle::close() noexcept
{
	if (!handle_)
		return;
	schedule_close(static_cast<NativeHandle>(handle_));
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
		Version plugin_v = unpack_version(abi_ver_fn());
		if (!compatible(plugin_v, THORAX_VERSION))
		{
			std::ostringstream msg;
			msg << "Plugin built against thorax "
			    << plugin_v.major << '.' << plugin_v.minor << '.' << plugin_v.patch
			    << " is not compatible with host "
			    << THORAX_VERSION.major << '.' << THORAX_VERSION.minor << '.' << THORAX_VERSION.patch
			    << ": " << path;
			native_close(h);
			return Result<PluginHandle, Error>::err({ErrorCode::VersionMismatch, msg.str()});
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
