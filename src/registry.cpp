/*
 *  Created by LuckyNeko on 22/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "thx/registry.h"

namespace thx
{

Registry& Registry::instance() noexcept
{
	static Registry inst;
	return inst;
}

bool initialise(std::string debugName)
{
	auto& reg = Registry::instance();
	if (!reg.m_debugName.empty())
		return false;
	reg.m_debugName = std::move(debugName);
	return true;
}

void shutdown() noexcept
{
	auto& reg = Registry::instance();
	reg.m_pluginGarbage.collect();
	reg.m_debugName.clear();
}

Registry& registry() noexcept
{
	return Registry::instance();
}

} // namespace thx
