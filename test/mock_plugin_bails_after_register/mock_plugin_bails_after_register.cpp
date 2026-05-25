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
#include <thx/plugin/iplugin.h>
#include <thx/plugin/platform.h>

#include <memory>

namespace
{

class BailsAfterRegisterPlugin : public thx::plugin::IPlugin
{
public:
	thx::StringView name()    const override { return "thx.mock.BailsAfterRegisterPlugin"; }
	thx::Version    version() const override { return thx::Version{1, 0, 0}; }

	bool onLoad(thx::service::ServiceManager& sm) override
	{
		sm.registerService<thx_mock::ServiceA>(
			[]() -> std::shared_ptr<thx::service::IService>
			{
				return std::make_shared<thx_mock::ServiceA>();
			});
		return false; // PluginManager must roll back the ServiceA registration.
	}

	void onUnload(thx::service::ServiceManager&) override {}

	thx::Span<const thx::service::ServiceID> provides() const override
	{
		static const thx::service::ServiceID kProvides[] = {
			thx_mock::ServiceA::staticId()
		};
		return thx::Span<const thx::service::ServiceID>(kProvides, 1);
	}
};

} // namespace

THX_DEFINE_PLUGIN(BailsAfterRegisterPlugin)
