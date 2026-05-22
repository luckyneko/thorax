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
		FileNotFound,
		SymbolNotFound,
		FactoryFailed,
		AlreadyLoaded,
		NotLoaded,
		VersionMismatch,
		RegistrationFailed,
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

		bool is_ok()  const noexcept { return data_.index() == 0; }
		bool is_err() const noexcept { return data_.index() == 1; }

		explicit operator bool() const noexcept { return is_ok(); }

		T&       value()       { return std::get<0>(data_); }
		T const& value() const { return std::get<0>(data_); }
		E&       error()       { return std::get<1>(data_); }
		E const& error() const { return std::get<1>(data_); }

		// Returns value() if ok, otherwise the supplied fallback.
		T value_or(T fallback) const&
		{
			return is_ok() ? value() : std::move(fallback);
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
			if (is_err())
				return Result<U, E>::err(error());
			return Result<U, E>::ok(std::forward<F>(f)(value()));
		}

	private:
		template <std::size_t I, typename... Args>
		explicit Result(std::in_place_index_t<I> tag, Args&&... args)
			: data_(tag, std::forward<Args>(args)...)
		{
		}

		std::variant<T, E> data_;
	};

	// Specialisation for operations that succeed with no value.
	// ok() carries no payload; err() carries an E.
	template <typename E>
	class Result<void, E>
	{
	public:
		static Result ok()         { return Result{};                          }
		static Result err(E error) { Result r; r.error_ = std::move(error); return r; }

		bool is_ok()  const noexcept { return !error_.has_value(); }
		bool is_err() const noexcept { return  error_.has_value(); }

		explicit operator bool() const noexcept { return is_ok(); }

		E&       error()       { return *error_; }
		E const& error() const { return *error_; }

	private:
		std::optional<E> error_;
	};

} // namespace thx
