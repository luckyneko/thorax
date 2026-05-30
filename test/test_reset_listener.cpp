/*
 *  Created by LuckyNeko on 30/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch_all.hpp>
#include <thx/lifecycle.h>

// Test isolation for the production-path tests.
//
// Tests that drive the framework through the public facades (thx::service::* /
// thx::plugin::*) share the one process-wide Registry singleton. This listener
// returns that singleton to an empty state after every test case — shutdown()
// unloads all plugins, unregisters all services, and drains the deferred-close
// queue — so cases stay isolated and order-independent without each one having
// to clean up by hand.
//
// Contract: a TEST_CASE MUST drop every ServiceHandle into a plugin DSO before
// its body returns. This listener runs *after* the body (so case-local handles
// are already destroyed) and shutdown() dlclose's those DSOs; a handle that
// outlived the case would run its service destructor in unmapped code.

namespace
{
	struct RegistryResetListener : Catch::EventListenerBase
	{
		using Catch::EventListenerBase::EventListenerBase;

		void testCaseEnded(Catch::TestCaseStats const&) override
		{
			thx::shutdown();
		}
	};
}

CATCH_REGISTER_LISTENER(RegistryResetListener)
