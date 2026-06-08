/*
 *  Created by LuckyNeko on 22/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "thx/rtti/type_name.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace thx::service
{
	// Stable cross-DSO service identifier. Constructed from a string literal;
	// equality is determined by comparing both the hash and the string content
	// so that hash collisions cannot produce false equality.
	//
	// Example: constexpr thx::service::ServiceID kFileService("thx.io.FileService");
	class ServiceID
	{
	public:
		constexpr explicit ServiceID(const char* name) noexcept
			: m_hash(fnv1aHash(name))
			, m_name(name)
		{
		}

		template <typename T>
		static constexpr ServiceID from() noexcept
		{
			return ServiceID(thx::rtti::TypeName<T>::value.data());
		}

		constexpr bool operator==(const ServiceID& other) const noexcept
		{
			return m_hash == other.m_hash &&
				   std::string_view(m_name) == std::string_view(other.m_name);
		}

		constexpr bool operator!=(const ServiceID& other) const noexcept
		{
			return !(*this == other);
		}

		constexpr uint64_t hash() const noexcept { return m_hash; }
		constexpr const char* name() const noexcept { return m_name; }

	private:
		// FNV-1a 64-bit. Internal helper for the constructor — kept private
		// because ServiceID is its only consumer.
		static constexpr uint64_t fnv1aHash(std::string_view str) noexcept
		{
			uint64_t hash = 14695981039346656037ULL;
			for (unsigned char c : str)
			{
				hash ^= static_cast<uint64_t>(c);
				hash *= 1099511628211ULL;
			}
			return hash;
		}

		uint64_t m_hash;
		const char* m_name;
	};

} // namespace thx::service

// std::hash specialisation so ServiceID can be used as an unordered_map key.
template <>
struct std::hash<thx::service::ServiceID>
{
	std::size_t operator()(const thx::service::ServiceID& id) const noexcept
	{
		return static_cast<std::size_t>(id.hash());
	}
};
