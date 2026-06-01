/*
 *  Created by LuckyNeko on 25/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include <thx/service/iservice.h>

namespace examples
{

	// Shared interface header — included by both the logging plugin and the host.
	struct ILoggingService : thx::service::Service<ILoggingService>
	{
		static constexpr thx::Version staticVersion()
		{
			return thx::Version{1, 0, 0};
		}

		virtual void log(const char* message) = 0;
	};

} // namespace examples
