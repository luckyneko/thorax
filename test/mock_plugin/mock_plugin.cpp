/*
 *  Created by LuckyNeko on 24/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "mock_plugin.h"
#include <thx/plugin/platform.h>

namespace
{

	struct MockServiceImpl : thx_mock::MockService
	{
		int ping() const override { return 42; }
	};

} // namespace

THX_DEFINE_SERVICE_PLUGIN(MockServiceImpl)
