/*
 *  Created by LuckyNeko on 24/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include <thx/service.h>

// Minimal service interface used by PluginLoader integration tests.
// Both the mock_plugin shared library and the test binary include this header.
namespace thx_mock
{

struct MockService : thx::Service<MockService>
{
	static constexpr thx::Version static_version()
	{
		return thx::make_version(1, 0, 0);
	}

	virtual int ping() const = 0;
};

} // namespace thx_mock
