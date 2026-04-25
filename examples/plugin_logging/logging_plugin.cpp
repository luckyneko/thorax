/*
 *  Created by LuckyNeko on 25/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "interfaces/logging_service.h"
#include <thx/platform.h>

#include <cstdio>

namespace
{

struct LoggingServiceImpl : examples::ILoggingService
{
	void log(const char* message) override
	{
		std::printf("[log] %s\n", message);
		std::fflush(stdout);
	}
};

} // namespace

THX_DEFINE_SERVICE_PLUGIN(LoggingServiceImpl)
