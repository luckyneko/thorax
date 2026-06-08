/*
 *  Created by LuckyNeko on 30/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch_all.hpp>
#include <thx/thorax.h>

// Test isolation for the production-path tests.
//
// Tests that drive the framework through the public facades (thx::service::* /
// thx::plugin::*) share the one process-wide Registry singleton. The framework
// requires thx::initialise() before any facade works, so this listener stands
// the singleton up before every case and tears it back down after — shutdown()
// unloads all plugins, unregisters all services, and drains the deferred-close
// queue — so cases start from an empty Registry and stay order-independent
// without each one having to set up or clean up by hand.
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

		void testCaseStarting(const Catch::TestCaseInfo&) override
		{
			thx::initialise();
		}

		void testCaseEnded(const Catch::TestCaseStats&) override
		{
			thx::shutdown();
		}
	};
} // namespace

CATCH_REGISTER_LISTENER(RegistryResetListener)
