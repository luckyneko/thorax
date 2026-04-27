/*
 *  Created by LuckyNeko on 24/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

// Intentionally exports thx_abi_version() returning 999 to trigger the
// PluginHandle::open() version-mismatch rejection path.

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
	thx::Version    version() const override { return thx::make_version(1, 0, 0); }

	bool onLoad(thx::ServiceManager&)   override { return true; }
	void onUnload(thx::ServiceManager&) override {}
};

} // namespace

extern "C" THX_PLUGIN_EXPORT thx::IPlugin* thx_create_plugin()
{
	return new (std::nothrow) BadAbiPlugin();
}
extern "C" THX_PLUGIN_EXPORT void thx_destroy_plugin(thx::IPlugin* p)
{
	delete p;
}
extern "C" THX_PLUGIN_EXPORT uint32_t thx_abi_version()
{
	return 999u;
}
