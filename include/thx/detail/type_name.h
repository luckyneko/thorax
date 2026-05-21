/*
 *  Created by LuckyNeko on 23/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace thx
{
	namespace detail
	{

		// Returns a string_view of the C++ qualified name for T, extracted from
		// the compiler's function-signature macro. The backing storage is the
		// string literal inside the instantiation of this function template, which
		// has static storage duration.
		//
		// Examples (after extraction):
		//   extract_type_name<thx::io::FileService>() -> "thx::io::FileService"
		//   extract_type_name<int>()                   -> "int"
		template <typename T>
		constexpr std::string_view extract_type_name() noexcept
		{
#if defined(_MSC_VER) && !defined(__clang__)
			// __FUNCSIG__ example:
			//   "... __cdecl thx::detail::extract_type_name<class thx::io::FileService>(void)"
			// Strategy: find "extract_type_name<" then match angle brackets to
			// locate the closing '>'.
			std::string_view fn{__FUNCSIG__};
			constexpr std::string_view marker = "extract_type_name<";
			auto mpos = fn.find(marker);
			// Defensive: if a future MSVC ever stops emitting the marker, return
			// an empty string_view rather than walking off the end. Empty names
			// are observable downstream (the ServiceID will have an empty name)
			// which surfaces the problem instead of UB.
			if (mpos == std::string_view::npos)
				return {};
			auto start = mpos + marker.size();

			// Walk forward counting '<' and '>' to find the matching '>'.
			std::size_t end = start;
			int depth = 1;
			while (end < fn.size() && depth > 0)
			{
				if (fn[end] == '<')
					++depth;
				else if (fn[end] == '>')
				{
					--depth;
					if (depth == 0)
						break;
				}
				++end;
			}

			auto raw = fn.substr(start, end - start);

			// MSVC prepends "class " or "struct " to user-defined types.
			constexpr std::size_t kClassLen  = std::size_t{6}; // "class "
			constexpr std::size_t kStructLen = std::size_t{7}; // "struct "
			if (raw.size() >= kClassLen && raw.substr(std::size_t{0}, kClassLen) == "class ")
				return raw.substr(kClassLen);
			if (raw.size() >= kStructLen && raw.substr(std::size_t{0}, kStructLen) == "struct ")
				return raw.substr(kStructLen);
			return raw;

#else
			// GCC __PRETTY_FUNCTION__ example:
			//   "... [with T = thx::io::FileService; std::string_view = ...]"
			// Clang __PRETTY_FUNCTION__ example:
			//   "... [T = thx::io::FileService]"
			// Both contain "T = " followed by the type name, terminated by ';' or ']'.
			std::string_view fn{__PRETTY_FUNCTION__};
			auto pos = fn.find("T = ");
			if (pos == std::string_view::npos)
				return {};
			auto start = pos + 4;
			auto end = fn.find_first_of(";]", start);
			if (end == std::string_view::npos)
				end = fn.size();
			return fn.substr(start, end - start);
#endif
		}

		// Returns the number of characters in `name` after replacing each "::"
		// with a single '.'.
		constexpr std::size_t dotted_length(std::string_view name) noexcept
		{
			std::size_t len = 0, i = 0;
			while (i < name.size())
			{
				if (i + 1 < name.size() && name[i] == ':' && name[i + 1] == ':')
				{
					++len;
					i += 2;
				}
				else
				{
					++len;
					++i;
				}
			}
			return len;
		}

		// Returns a null-terminated char array containing `name` with every "::"
		// replaced by '.'. Template parameter N must equal dotted_length(name).
		template <std::size_t N>
		constexpr std::array<char, N + 1> make_dotted(std::string_view name) noexcept
		{
			std::array<char, N + 1> result{};
			std::size_t j = 0, i = 0;
			while (i < name.size())
			{
				if (i + 1 < name.size() && name[i] == ':' && name[i + 1] == ':')
				{
					result[j++] = '.';
					i += 2;
				}
				else
				{
					result[j++] = name[i++];
				}
			}
			result[j] = '\0';
			return result;
		}

		// Provides the dot-separated service name for type T as a constexpr
		// null-terminated char array with static storage duration.
		//
		// TypeName<thx::io::FileService>::value  contains  "thx.io.FileService\0"
		// TypeName<MyService>::value             contains  "MyService\0"
		template <typename T>
		struct TypeName
		{
			static constexpr std::string_view raw = extract_type_name<T>();
			static constexpr std::size_t len = dotted_length(raw);
			static constexpr auto value = make_dotted<len>(raw);
		};

	} // namespace detail
} // namespace thx
