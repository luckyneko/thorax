/*
 *  Created by LuckyNeko on 24/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

// Intentionally exports thx_abi_version() returning 999 to trigger the
// PluginHandle::open() version-mismatch rejection path.

#include <thx/iservice.h>
#include <thx/service_id.h>
#include <thx/version_type.h>

#include <cstdint>
#include <new>

namespace
{

struct BadAbiService : thx::IService
{
	thx::ServiceID id()      const override { return thx::ServiceID("test.BadAbi"); }
	thx::Version   version() const override { return thx::make_version(1, 0, 0);    }
};

} // namespace

extern "C" thx::IService* thx_create()             { return new (std::nothrow) BadAbiService(); }
extern "C" void           thx_destroy(thx::IService* p) { delete p; }
extern "C" uint32_t       thx_abi_version()         { return 999u; }
