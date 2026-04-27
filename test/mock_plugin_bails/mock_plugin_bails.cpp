/*
 *  Created by LuckyNeko on 25/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

// IPlugin-based mock plugin whose onLoad always returns false.
// Used to verify that PluginLoader::load propagates the failure.

#include <thx/iplugin.h>
#include <thx/platform.h>

namespace
{

class BailingPlugin : public thx::IPlugin
{
public:
	thx::StringView name()    const override { return "thx.mock.BailingPlugin"; }
	thx::Version    version() const override { return thx::make_version(1, 0, 0); }

	bool onLoad(thx::ServiceManager&)   override { return false; }
	void onUnload(thx::ServiceManager&) override {}
};

} // namespace

THX_DEFINE_CUSTOM_PLUGIN(BailingPlugin)
