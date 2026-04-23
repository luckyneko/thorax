/*
 *  Created by LuckyNeko on 22/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "thx/detail/hash.h"
#include "thx/detail/type_name.h"

namespace thx
{
	// Stable cross-DSO service identifier. Constructed from a string literal;
	// equality is determined by comparing both the hash and the string content
	// so that hash collisions cannot produce false equality.
	//
	// Example: constexpr thx::ServiceID kFileService("thx.io.FileService");
	class ServiceID
	{
	public:
		constexpr explicit ServiceID(const char* name) noexcept
			: hash_(detail::fnv1a_hash(name))
			, name_(name)
		{
		}

		template <typename T>
		static constexpr ServiceID from() noexcept
		{
			return ServiceID(detail::TypeName<T>::value.data());
		}

		constexpr bool operator==(ServiceID const& other) const noexcept
		{
			return hash_ == other.hash_ &&
				   std::string_view(name_) == std::string_view(other.name_);
		}

		constexpr bool operator!=(ServiceID const& other) const noexcept
		{
			return !(*this == other);
		}

		constexpr uint64_t hash() const noexcept { return hash_; }
		constexpr const char* name() const noexcept { return name_; }

	private:
		uint64_t hash_;
		const char* name_;
	};

} // namespace thx

// std::hash specialisation so ServiceID can be used as an unordered_map key.
template <>
struct std::hash<thx::ServiceID>
{
	std::size_t operator()(thx::ServiceID const& id) const noexcept
	{
		return static_cast<std::size_t>(id.hash());
	}
};
