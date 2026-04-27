/*
 *  Created by LuckyNeko on 25/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

// IPlugin-based mock plugin used to verify:
//  - a single DSO can register multiple services in onLoad
//  - required() service IDs are honoured by PluginLoader
//
// Declares a required() dependency on thx_mock::MockService and registers
// thx_mock::ServiceA and thx_mock::ServiceB in onLoad.

#include "mock_plugin.h"
#include <thx/iplugin.h>
#include <thx/platform.h>

#include <memory>

namespace
{

class MultiPlugin : public thx::IPlugin
{
public:
	thx::StringView name()    const override { return "thx.mock.MultiPlugin"; }
	thx::Version    version() const override { return thx::make_version(1, 0, 0); }

	bool onLoad(thx::ServiceManager& sm) override
	{
		bool a = sm.register_service<thx_mock::ServiceA>(
			[]() -> std::shared_ptr<thx::IService>
			{
				return std::make_shared<thx_mock::ServiceA>();
			});
		bool b = sm.register_service<thx_mock::ServiceB>(
			[]() -> std::shared_ptr<thx::IService>
			{
				return std::make_shared<thx_mock::ServiceB>();
			});
		return a && b;
	}

	void onUnload(thx::ServiceManager& sm) override
	{
		sm.unregister_service<thx_mock::ServiceB>();
		sm.unregister_service<thx_mock::ServiceA>();
	}

	thx::Span<const thx::ServiceID> required() const override
	{
		static const thx::ServiceID kReqs[] = { thx_mock::MockService::static_id() };
		return thx::Span<const thx::ServiceID>(kReqs, 1);
	}
};

} // namespace

THX_DEFINE_CUSTOM_PLUGIN(MultiPlugin)
