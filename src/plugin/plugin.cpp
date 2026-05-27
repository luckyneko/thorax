/*
 *  Created by LuckyNeko on 25/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "thx/plugin/plugin.h"
#include "plugin/plugin_manager.h"
#include "registry.h"

namespace thx::plugin
{

Result<void, Error> discover(std::string const& directory, Recursive recursive)
{
	return thx::Registry::instance().pluginManager().discover(directory, recursive);
}

Result<void, Error> forget(std::string const& path)
{
	return thx::Registry::instance().pluginManager().forget(path);
}

Result<void, Error> open(std::string const& path)
{
	return thx::Registry::instance().pluginManager().open(path);
}

Result<void, Error> close(std::string const& path)
{
	return thx::Registry::instance().pluginManager().close(path);
}

std::size_t closeAllOpened()
{
	return thx::Registry::instance().pluginManager().closeAllOpened();
}

Result<void, Error> load(std::string const& path)
{
	return thx::Registry::instance().pluginManager().load(path);
}

Result<void, Error> unload(std::string const& path)
{
	return thx::Registry::instance().pluginManager().unload(path);
}

Result<void, Error> reload(std::string const& path)
{
	return thx::Registry::instance().pluginManager().reload(path);
}

LoadSummary discoverAndLoad(std::string const& directory, Recursive recursive)
{
	return thx::Registry::instance().pluginManager().discoverAndLoad(directory, recursive);
}

Result<void, Error> checkRequirements(Span<const ServiceRequirement> reqs)
{
	return PluginManager::checkRequirements(
	    thx::Registry::instance().serviceManager(), reqs);
}

std::vector<PluginInfo> plugins()
{
	return thx::Registry::instance().pluginManager().plugins();
}

std::vector<PluginInfo> plugins(State state)
{
	return thx::Registry::instance().pluginManager().plugins(state);
}

std::optional<PluginInfo> pluginInfo(std::string const& path)
{
	return thx::Registry::instance().pluginManager().pluginInfo(path);
}

bool is(State state, std::string const& path)
{
	return thx::Registry::instance().pluginManager().is(state, path);
}

bool isDiscovered(std::string const& path)
{
	return thx::Registry::instance().pluginManager().isDiscovered(path);
}

bool isOpened(std::string const& path)
{
	return thx::Registry::instance().pluginManager().isOpened(path);
}

bool isLoaded(std::string const& path)
{
	return thx::Registry::instance().pluginManager().isLoaded(path);
}

std::vector<PluginInfo> pluginsProviding(std::string const& serviceId)
{
	return thx::Registry::instance().pluginManager().pluginsProviding(serviceId);
}

std::size_t collectGarbage() noexcept
{
	return thx::Registry::instance().pluginGarbage().collect();
}

std::size_t pendingGarbage() noexcept
{
	return thx::Registry::instance().pluginGarbage().pending();
}

} // namespace thx::plugin
