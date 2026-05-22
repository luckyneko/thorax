/*
 *  Created by LuckyNeko on 24/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include <thx/service/service.h>

// Minimal service interface used by PluginManager integration tests.
// Both the mock_plugin shared library and the test binary include this header.
namespace thx_mock
{

struct MockService : thx::Service<MockService>
{
	static constexpr thx::Version staticVersion()
	{
		return thx::Version{1, 0, 0};
	}

	virtual int ping() const = 0;
};

// Additional services registered by mock_plugin_multi (used by the
// IPlugin-multi-service tests).
struct ServiceA : thx::Service<ServiceA>
{
	static constexpr thx::Version staticVersion()
	{
		return thx::Version{1, 0, 0};
	}
};

struct ServiceB : thx::Service<ServiceB>
{
	static constexpr thx::Version staticVersion()
	{
		return thx::Version{1, 0, 0};
	}
};

} // namespace thx_mock
