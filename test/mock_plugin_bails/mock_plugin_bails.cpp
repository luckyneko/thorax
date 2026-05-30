/*
 *  Created by LuckyNeko on 25/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

// IPlugin-based mock plugin whose onLoad always returns false.
// Used to verify that PluginManager::load propagates the failure.

#include <thx/plugin/iplugin.h>
#include <thx/plugin/platform.h>

namespace
{

	class BailingPlugin : public thx::plugin::IPlugin
	{
	public:
		thx::StringView name() const override { return "thx.mock.BailingPlugin"; }
		thx::Version version() const override { return thx::Version{1, 0, 0}; }

		bool onLoad() override { return false; }
		void onUnload() override {}
	};

} // namespace

THX_DEFINE_PLUGIN(BailingPlugin)
