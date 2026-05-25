/*
 *  Created by LuckyNeko on 21/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

// IPlugin-based mock that registers ServiceA in onLoad but forgets to
// unregister it in onUnload. PluginManager's safety-net sweep must catch
// the survivor and unregister it on the user's behalf, emitting a Warn
// diagnostic.

#include "mock_plugin.h"
#include <thx/plugin/iplugin.h>
#include <thx/plugin/platform.h>

#include <memory>

namespace
{

class ForgetsUnloadPlugin : public thx::plugin::IPlugin
{
public:
	thx::StringView name()    const override { return "thx.mock.ForgetsUnloadPlugin"; }
	thx::Version    version() const override { return thx::Version{1, 0, 0}; }

	bool onLoad(thx::service::ServiceManager& sm) override
	{
		return sm.registerService<thx_mock::ServiceA>(
			[]() -> std::shared_ptr<thx::service::IService>
			{
				return std::make_shared<thx_mock::ServiceA>();
			});
	}

	// Intentionally empty: simulate a misbehaved plugin that forgets to
	// unregister what it registered.
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

THX_DEFINE_PLUGIN(ForgetsUnloadPlugin)
