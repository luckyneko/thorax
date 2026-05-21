/*
 *  Created by LuckyNeko on 22/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "thx/iservice.h"

namespace thx
{
	// CRTP base that wires the IService virtual interface to static metadata on
	// the derived type. Derived must provide:
	//
	//   static constexpr thx::Version static_version();
	//
	// static_id() has a default implementation that derives the ID from the
	// C++ qualified name (e.g. thx::io::FileService -> "thx.io.FileService").
	// Derived may override it with an explicit ID if a custom name is preferred.
	//
	// Example:
	//
	//   class ImageLoadingService : public thx::Service<ImageLoadingService>
	//   {
	//   public:
	//       static constexpr thx::Version static_version()
	//           { return thx::Version{1, 0, 0}; }
	//
	//       // Service-specific API:
	//       virtual void add_loader(std::shared_ptr<IImageLoader>) = 0;
	//   };
	//
	// Plugin B includes this interface header and calls:
	//   sm.get_service<ImageLoadingService>()
	// with no knowledge of Plugin A's implementation.
	template <typename Derived>
	class Service : public IService
	{
	public:
		// Default: converts the C++ qualified name to dot-notation.
		// Override in Derived if a different canonical name is needed.
		static constexpr ServiceID static_id() noexcept
		{
			return ServiceID::from<Derived>();
		}

		// Must be provided by Derived, e.g.:
		//   static constexpr thx::Version static_version()
		//       { return thx::Version{1, 0, 0}; }

		// final prevents downstream types from changing the identity of an
		// already-defined service interface.
		ServiceID id() const final { return Derived::static_id(); }
		Version version() const final { return Derived::static_version(); }
	};

} // namespace thx
