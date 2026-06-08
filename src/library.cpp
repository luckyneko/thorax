/*
 *  Created by LuckyNeko on 22/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "library.h"

#include <utility>

#if defined(_WIN32)
#	define WIN32_LEAN_AND_MEAN
#	include <windows.h>
#else
#	include <dlfcn.h>
#endif

namespace thx
{

	namespace
	{

#if defined(_WIN32)
		void* nativeOpen(const char* path, Library::LoadFlags /*flags*/) noexcept
		{
			// LoadLibrary doesn't expose a Lazy/Now split — symbol resolution
			// happens at load time regardless. Strict and Lazy converge here.
			return LoadLibraryExA(path, nullptr, 0);
		}
		void nativeClose(void* handle) noexcept
		{
			FreeLibrary(static_cast<HMODULE>(handle));
		}
		void* nativeSym(void* handle, const char* name) noexcept
		{
			return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle), name));
		}
		std::string nativeError() noexcept
		{
			char buf[256] = {};
			FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
						   nullptr, GetLastError(), 0, buf, static_cast<DWORD>(sizeof(buf)), nullptr);
			return buf;
		}
#else
		void* nativeOpen(const char* path, Library::LoadFlags flags) noexcept
		{
			int mode = (flags == Library::LoadFlags::Strict) ? RTLD_NOW : RTLD_LAZY;
			return dlopen(path, mode | RTLD_LOCAL);
		}
		void nativeClose(void* handle) noexcept
		{
			dlclose(handle);
		}
		void* nativeSym(void* handle, const char* name) noexcept
		{
			return dlsym(handle, name);
		}
		std::string nativeError() noexcept
		{
			const char* msg = dlerror();
			return msg ? std::string(msg) : std::string{};
		}
#endif

	} // namespace

	Library::~Library()
	{
		close();
	}

	Library::Library(Library&& other) noexcept
		: m_handle(other.m_handle)
		, m_valid(other.m_valid)
		, m_path(std::move(other.m_path))
		, m_error(std::move(other.m_error))
	{
		other.m_handle = nullptr;
		other.m_valid = false;
	}

	Library& Library::operator=(Library&& other) noexcept
	{
		if (this != &other)
		{
			close();
			m_handle = other.m_handle;
			m_valid = other.m_valid;
			m_path = std::move(other.m_path);
			m_error = std::move(other.m_error);
			other.m_handle = nullptr;
			other.m_valid = false;
		}
		return *this;
	}

	Library& Library::open(const std::filesystem::path& path, LoadFlags flags)
	{
		if (m_handle)
			close();

		m_path = path.string();
		m_error.clear();

		m_handle = nativeOpen(m_path.c_str(), flags);
		if (!m_handle)
		{
			m_valid = false;
			m_error = nativeError();
			if (m_error.empty())
				m_error = "failed to open: " + m_path;
			return *this;
		}

		m_valid = true;
		return *this;
	}

	void Library::close() noexcept
	{
		if (m_handle)
		{
			nativeClose(m_handle);
			m_handle = nullptr;
		}
		m_valid = false;
		// m_path and m_error are retained so the caller can still inspect them
		// after a close(); they're reset on the next open().
	}

	void* Library::release() noexcept
	{
		void* h = m_handle;
		m_handle = nullptr;
		m_valid = false;
		return h;
	}

	void* Library::sym(const char* name) noexcept
	{
		if (!m_handle || !name)
			return nullptr;
		void* p = nativeSym(m_handle, name);
		if (!p)
			m_error = nativeError();
		return p;
	}

} // namespace thx
