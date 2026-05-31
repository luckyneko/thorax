/*
 *  Created by LuckyNeko on 31/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include <thx/service/iservice.h>

namespace examples
{

	// Shared interface header — included by both the greeter plugin and the host.
	//
	// The greeter plugin depends on ILoggingService: it emits its greeting
	// through whatever logging service is registered. That dependency is
	// declared in the plugin's IPlugin::required(), so a host must arrange for
	// a logging provider to be loaded first — which is exactly what
	// thx::plugin::loadWithDependencies does automatically.
	struct IGreeterService : thx::service::Service<IGreeterService>
	{
		static constexpr thx::Version staticVersion()
		{
			return thx::Version{1, 0, 0};
		}

		virtual void greet(const char* who) = 0;
	};

} // namespace examples
