/*
 *  Created by LuckyNeko on 25/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

// IPlugin-based mock plugin used to verify:
//  - a single DSO can register multiple services in onLoad
//  - required() service IDs are honoured by PluginManager
//
// Declares a required() dependency on thx_mock::MockService and registers
// thx_mock::ServiceA and thx_mock::ServiceB in onLoad.

#include "mock_plugin.h"
#include <thx/plugin/iplugin.h>
#include <thx/plugin/platform.h>

#include <memory>

namespace
{

class MultiPlugin : public thx::plugin::IPlugin
{
public:
	thx::StringView name()    const override { return "thx.mock.MultiPlugin"; }
	thx::Version    version() const override { return thx::Version{1, 0, 0}; }

	bool onLoad(thx::service::ServiceManager& sm) override
	{
		bool a = sm.registerService<thx_mock::ServiceA>(
			[]() -> std::shared_ptr<thx::service::IService>
			{
				return std::make_shared<thx_mock::ServiceA>();
			});
		bool b = sm.registerService<thx_mock::ServiceB>(
			[]() -> std::shared_ptr<thx::service::IService>
			{
				return std::make_shared<thx_mock::ServiceB>();
			});
		return a && b;
	}

	void onUnload(thx::service::ServiceManager& sm) override
	{
		sm.unregisterService<thx_mock::ServiceB>();
		sm.unregisterService<thx_mock::ServiceA>();
	}

	thx::Span<const thx::plugin::ServiceRequirement> required() const override
	{
		static const thx::plugin::ServiceRequirement kReqs[] = {
			{ thx_mock::MockService::staticId(), thx_mock::MockService::staticVersion() }
		};
		return thx::Span<const thx::plugin::ServiceRequirement>(kReqs, 1);
	}
};

} // namespace

THX_DEFINE_PLUGIN(MultiPlugin)
