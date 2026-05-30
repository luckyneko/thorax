/*
 *  Created by LuckyNeko on 27/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

// IPlugin-based mock that declares a versioned dependency on MockService 2.0.0,
// which is higher than the version mock_plugin actually registers (1.0.0).
// PluginManager should reject the load with VersionMismatch.

#include "mock_plugin.h"
#include <thx/plugin/iplugin.h>
#include <thx/plugin/platform.h>

namespace
{

	class RequiresNewerPlugin : public thx::plugin::IPlugin
	{
	public:
		thx::StringView name() const override { return "thx.mock.RequiresNewerPlugin"; }
		thx::Version version() const override { return thx::Version{1, 0, 0}; }

		bool onLoad() override { return true; }
		void onUnload() override {}

		thx::Span<const thx::plugin::ServiceRequirement> required() const override
		{
			// Require MockService at 2.0.0; the registered MockService is only 1.0.0.
			static const thx::plugin::ServiceRequirement kReqs[] = {
				{thx_mock::MockService::staticId(), thx::Version{2, 0, 0}}};
			return thx::Span<const thx::plugin::ServiceRequirement>(kReqs, 1);
		}
	};

} // namespace

THX_DEFINE_PLUGIN(RequiresNewerPlugin)
