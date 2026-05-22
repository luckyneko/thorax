/*
 *  Created by LuckyNeko on 21/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

// IPlugin-based mock that registers ServiceA in onLoad and then returns false.
// PluginManager must reverse the partial registration so the registry is left
// in the same state as before the failed load.

#include "mock_plugin.h"
#include <thx/iplugin.h>
#include <thx/platform.h>

#include <memory>

namespace
{

class BailsAfterRegisterPlugin : public thx::IPlugin
{
public:
	thx::StringView name()    const override { return "thx.mock.BailsAfterRegisterPlugin"; }
	thx::Version    version() const override { return thx::Version{1, 0, 0}; }

	bool onLoad(thx::ServiceManager& sm) override
	{
		sm.register_service<thx_mock::ServiceA>(
			[]() -> std::shared_ptr<thx::IService>
			{
				return std::make_shared<thx_mock::ServiceA>();
			});
		return false; // PluginManager must roll back the ServiceA registration.
	}

	void onUnload(thx::ServiceManager&) override {}
};

} // namespace

THX_DEFINE_PLUGIN(BailsAfterRegisterPlugin)
