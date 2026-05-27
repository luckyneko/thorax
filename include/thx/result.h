/*
 *  Created by LuckyNeko on 24/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace thx
{
	// Error codes used by PluginManager and other library operations.
	enum class ErrorCode
	{
		Unknown = 0,
		FileNotFound,          // path does not exist on disk
		OpenFailed,            // file exists but the platform loader rejected it (permissions, missing transitive deps, malformed DSO, …)
		SymbolNotFound,
		FactoryFailed,
		NotLoaded,
		VersionMismatch,
		RegistrationFailed,
		InUse,                 // operation rejected because the target is still in active use
		MalformedManifest,     // sidecar JSON could not be parsed or has wrong shape
		ManifestMismatch,      // manifest's declared name/version/requires/provides disagrees with the live IPlugin
	};

	// Lightweight error descriptor returned (not thrown) by fallible operations.
	struct Error
	{
		ErrorCode   code{ErrorCode::Unknown};
		std::string message;
	};

	// Lightweight discriminated union: either a success value T or an Error.
	//
	// Construction:
	//   Result<int, Error>::ok(42)
	//   Result<int, Error>::err({ErrorCode::NotLoaded, "..."})
	//
	// Checking:
	//   if (auto r = loader.load(path); !r) { log(r.error().message); }
	template <typename T, typename E = Error>
	class Result
	{
	public:
		static Result ok(T value)
		{
			return Result{std::in_place_index<0>, std::move(value)};
		}

		static Result err(E error)
		{
			return Result{std::in_place_index<1>, std::move(error)};
		}

		bool isOk()  const noexcept { return m_data.index() == 0; }
		bool isErr() const noexcept { return m_data.index() == 1; }

		explicit operator bool() const noexcept { return isOk(); }

		T&       value()       { return std::get<0>(m_data); }
		T const& value() const { return std::get<0>(m_data); }
		E&       error()       { return std::get<1>(m_data); }
		E const& error() const { return std::get<1>(m_data); }

		// Returns value() if ok, otherwise the supplied fallback.
		T valueOr(T fallback) const&
		{
			return isOk() ? value() : std::move(fallback);
		}

		// Applies f to the contained value if ok, returning a new Result with
		// the transformed type. On error, propagates the error unchanged.
		//
		// Example:
		//   Result<int>::ok(2).map([](int n) { return n * 10; })  // → Result<int>::ok(20)
		template <typename F>
		auto map(F&& f) const& -> Result<std::decay_t<std::invoke_result_t<F, T const&>>, E>
		{
			using U = std::decay_t<std::invoke_result_t<F, T const&>>;
			if (isErr())
				return Result<U, E>::err(error());
			return Result<U, E>::ok(std::forward<F>(f)(value()));
		}

	private:
		template <std::size_t I, typename... Args>
		explicit Result(std::in_place_index_t<I> tag, Args&&... args)
			: m_data(tag, std::forward<Args>(args)...)
		{
		}

		std::variant<T, E> m_data;
	};

	// Specialisation for operations that succeed with no value.
	// ok() carries no payload; err() carries an E.
	template <typename E>
	class Result<void, E>
	{
	public:
		static Result ok()         { return Result{};                          }
		static Result err(E error) { Result r; r.m_error = std::move(error); return r; }

		bool isOk()  const noexcept { return !m_error.has_value(); }
		bool isErr() const noexcept { return  m_error.has_value(); }

		explicit operator bool() const noexcept { return isOk(); }

		E&       error()       { return *m_error; }
		E const& error() const { return *m_error; }

	private:
		std::optional<E> m_error;
	};

} // namespace thx
