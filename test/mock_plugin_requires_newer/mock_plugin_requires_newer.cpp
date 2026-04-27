/*
 *  Created by LuckyNeko on 27/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

// IPlugin-based mock that declares a versioned dependency on MockService 2.0.0,
// which is higher than the version mock_plugin actually registers (1.0.0).
// PluginLoader should reject the load with VersionMismatch.

#include "mock_plugin.h"
#include <thx/iplugin.h>
#include <thx/platform.h>

namespace
{

class RequiresNewerPlugin : public thx::IPlugin
{
public:
	thx::StringView name()    const override { return "thx.mock.RequiresNewerPlugin"; }
	thx::Version    version() const override { return thx::make_version(1, 0, 0); }

	bool onLoad(thx::ServiceManager&)   override { return true; }
	void onUnload(thx::ServiceManager&) override {}

	thx::Span<const thx::ServiceRequirement> required() const override
	{
		// Require MockService at 2.0.0; the registered MockService is only 1.0.0.
		static const thx::ServiceRequirement kReqs[] = {
			{ thx_mock::MockService::static_id(), thx::make_version(2, 0, 0) }
		};
		return thx::Span<const thx::ServiceRequirement>(kReqs, 1);
	}
};

} // namespace

THX_DEFINE_PLUGIN(RequiresNewerPlugin)
