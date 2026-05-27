/*
 *  Created by LuckyNeko on 22/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "thx/result.h"
#include "thx/thx_api.h"

#include <filesystem>
#include <string>
#include <type_traits>

namespace thx
{
	// Platform shared-library file extension. Used by PluginManager::discover()
	// and any consumer that scans a directory for native modules.
#if defined(_WIN32)
	inline constexpr char const* LIBRARY_EXTENSION = ".dll";
#elif defined(__APPLE__)
	inline constexpr char const* LIBRARY_EXTENSION = ".dylib";
#else
	inline constexpr char const* LIBRARY_EXTENSION = ".so";
#endif

	// RAII wrapper around a platform dynamic shared object (DSO) handle.
	//
	// Move-only. The destructor closes the DSO synchronously (dlclose /
	// FreeLibrary). If you need *deferred* close — e.g. callers may still be
	// holding references into the DSO when you let go — move the Library into
	// the process-wide PluginGarbage queue instead. PluginHandle does this
	// automatically for plugin DSOs; direct users of Library don't have to.
	//
	// API shape: open() is fluent and returns *this, as is bind(). Validity is
	// queried via valid() / operator bool() / error(). bind() on an invalid
	// Library is a no-op (subsequent symbol lookups are skipped once the
	// chain has failed).
	//
	// Example:
	//   thx::Library lib;
	//   FooFn foo = nullptr;
	//   BarFn bar = nullptr;
	//   lib.open(path).bind("foo", foo).bind("bar", bar);
	//   if (!lib) { logError(lib.error()); return; }
	class THX_API Library
	{
	public:
		// Symbol-resolution policy for open().
		//
		// Lazy   — defer resolution to first symbol use (POSIX: RTLD_LAZY,
		//          Windows: default LoadLibrary behaviour). Cheaper open;
		//          unresolved symbols surface at the first call into them.
		// Strict — resolve everything at open time (POSIX: RTLD_NOW; Windows:
		//          treated the same as Lazy because LoadLibrary doesn't have
		//          a Now/Lazy split — the dynamic linker resolves
		//          dependencies on open). Strict hosts get fail-fast errors
		//          when a plugin has missing dependencies.
		enum class LoadFlags
		{
			Lazy,
			Strict,
		};

		Library() noexcept = default;
		~Library();

		Library(Library&&) noexcept;
		Library& operator=(Library&&) noexcept;

		Library(Library const&)            = delete;
		Library& operator=(Library const&) = delete;

		// Opens the DSO at path. If the Library is already holding an open
		// handle, closes it first. After the call, valid() reflects success
		// and error() carries any platform diagnostic.
		Library& open(std::filesystem::path const& path,
		              LoadFlags flags = LoadFlags::Lazy);

		// Closes the DSO if open. Idempotent and noexcept; calls dlclose /
		// FreeLibrary synchronously.
		void close() noexcept;

		// Resolves the named symbol into out. Fluent: returns *this. On any
		// failure (library not open, symbol missing) sets out to nullptr,
		// clears valid(), and records error(). Subsequent bind() calls on
		// an invalid Library are no-ops and leave their out-params untouched.
		template <typename FnPtr>
		Library& bind(char const* name, FnPtr& out)
		{
			static_assert(std::is_pointer_v<FnPtr>,
			    "Library::bind expects a function-pointer out-parameter");
			if (!m_valid)
				return *this;
			void* raw = sym(name);
			if (!raw)
			{
				m_valid = false;
				return *this;
			}
			out = reinterpret_cast<FnPtr>(raw);
			return *this;
		}

		// Result-returning variant of bind() for callers that want per-symbol
		// diagnostics rather than the fluent valid()-bit pattern. Does NOT
		// mutate the Library's validity bit — succeeds or fails per call,
		// independent of any prior bind() outcome (as long as the library is
		// open; if it isn't, returns NotLoaded).
		template <typename FnPtr>
		Result<void, Error> tryBind(char const* name, FnPtr& out)
		{
			static_assert(std::is_pointer_v<FnPtr>,
			    "Library::tryBind expects a function-pointer out-parameter");
			if (!m_handle)
				return Result<void, Error>::err({ErrorCode::NotLoaded,
					"Library::tryBind: library is not open"});
			void* raw = sym(name);
			if (!raw)
				return Result<void, Error>::err({ErrorCode::SymbolNotFound,
					std::string("Library::tryBind: symbol '") + (name ? name : "")
					+ "' not found in '" + m_path + "'"});
			out = reinterpret_cast<FnPtr>(raw);
			return Result<void, Error>::ok();
		}

		// True if the library is open AND every bind() so far has succeeded.
		bool valid() const noexcept { return m_valid; }
		explicit operator bool() const noexcept { return m_valid; }

		// Canonical path of the loaded DSO; empty until open() is called.
		std::string const& path() const noexcept { return m_path; }

		// Most recent platform error message. Empty if no error has occurred
		// since the last successful operation.
		std::string const& error() const noexcept { return m_error; }

		// Releases ownership of the native handle without closing it. After
		// this call, the Library is empty and invalid; the caller is
		// responsible for closing the returned handle (or e.g. moving it to
		// the PluginGarbage queue, which calls dlclose during collect()).
		void* release() noexcept;

		// Raw native handle, or nullptr if the Library isn't open. For
		// interop with platform-specific code; prefer bind() for symbol
		// resolution.
		void* nativeHandle() const noexcept { return m_handle; }

		// Raw typeless symbol lookup. Returns nullptr if the symbol is
		// missing or the library isn't open. Most callers should use bind()
		// instead — it does the type-cast and the validity bookkeeping in
		// one step.
		//
		// Side effect: on lookup failure, sym() updates error() with the
		// platform diagnostic. bind() relies on this — when a symbol lookup
		// inside bind() returns null, the bind clears its validity bit but
		// expects sym() to have already recorded the reason in m_error.
		// Direct callers of sym() will see the same behaviour.
		void* sym(char const* name) noexcept;

	private:
		void*       m_handle = nullptr;
		bool        m_valid  = false;
		std::string m_path;
		std::string m_error;
	};

} // namespace thx
