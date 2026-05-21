/*
 *  Created by LuckyNeko on 24/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

// Intentionally exports thx_abi_version() with a major component well outside
// any plausible host's, to trigger PluginHandle::open()'s version-mismatch
// rejection path.

#include <thx/iplugin.h>
#include <thx/platform.h>

#include <cstdint>
#include <new>

namespace
{

class BadAbiPlugin : public thx::IPlugin
{
public:
	thx::StringView name()    const override { return "test.BadAbi"; }
	thx::Version    version() const override { return thx::Version{1, 0, 0}; }

	bool onLoad(thx::ServiceManager&)   override { return true; }
	void onUnload(thx::ServiceManager&) override {}
};

} // namespace

THX_PLUGIN_API thx::IPlugin* thx_create_plugin()
{
	return new (std::nothrow) BadAbiPlugin();
}
THX_PLUGIN_API void thx_destroy_plugin(thx::IPlugin* p)
{
	delete p;
}
THX_PLUGIN_API uint32_t thx_abi_version()
{
	// Major = 99 in the upper 8 bits â€” guaranteed to mismatch any real host.
	return thx::Version{99, 0, 0}.pack();
}
