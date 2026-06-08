/*
 *  Created by LuckyNeko on 25/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "thx/plugin/plugin.h"
#include "plugin/plugin_handle.h"
#include "plugin/plugin_manager.h"
#include "registry.h"

#include <string_view>
#include <utility>

namespace thx::plugin
{

	Result<void, Error> discover(const std::string& directory, Recursive recursive)
	{
		return thx::registry()->pluginManager().discover(directory, recursive);
	}

	Result<void, Error> forget(const std::string& path)
	{
		return thx::registry()->pluginManager().forget(path);
	}

	Result<void, Error> open(const std::string& path)
	{
		return thx::registry()->pluginManager().open(path);
	}

	Result<void, Error> close(const std::string& path)
	{
		return thx::registry()->pluginManager().close(path);
	}

	std::size_t closeAllOpened()
	{
		return thx::registry()->pluginManager().closeAllOpened();
	}

	Result<void, Error> load(const std::string& path)
	{
		return thx::registry()->pluginManager().load(path);
	}

	Result<void, Error> unload(const std::string& path)
	{
		return thx::registry()->pluginManager().unload(path);
	}

	Result<void, Error> reload(const std::string& path)
	{
		return thx::registry()->pluginManager().reload(path);
	}

	LoadSummary discoverAndLoad(const std::string& directory, Recursive recursive)
	{
		return thx::registry()->pluginManager().discoverAndLoad(directory, recursive);
	}

	Result<PluginManifest, Error> inspect(const std::string& dsoPath)
	{
		// Manifestless inspection: open the DSO, instantiate the IPlugin, read
		// its metadata, tear the IPlugin down, queue the DSO for deferred close.
		// No PluginManager state involvement.
		auto handleResult = PluginHandle::open(dsoPath);
		if (!handleResult)
			return Result<PluginManifest, Error>::err(std::move(handleResult.error()));

		auto handle = std::move(handleResult.value());
		auto* destroy = handle.destroyFn();
		IPlugin* raw = handle.createFn()();
		if (!raw)
			return Result<PluginManifest, Error>::err({ErrorCode::FactoryFailed,
													   "thx_create_plugin returned null for: " + dsoPath});

		PluginManifest manifest;
		manifest.schema = 1;
		manifest.name = std::string(static_cast<std::string_view>(raw->name()));
		manifest.version = raw->version();

		auto provides = raw->provides();
		manifest.provides.reserve(provides.size());
		for (std::size_t i = 0; i < provides.size(); ++i)
			manifest.provides.emplace_back(provides[i].name());

		auto reqs = raw->required();
		manifest.requirements.reserve(reqs.size());
		for (std::size_t i = 0; i < reqs.size(); ++i)
			manifest.requirements.push_back({std::string(reqs[i].id.name()), reqs[i].version});

		destroy(raw);
		// ~PluginHandle queues the underlying Library into PluginGarbage; the
		// DSO unmaps at the next collectGarbage() (or program exit). No need
		// to drain here — inspect is a one-shot, not part of any sequence.
		return Result<PluginManifest, Error>::ok(std::move(manifest));
	}

	Result<void, Error> checkRequirements(Span<const ServiceRequirement> reqs)
	{
		return PluginManager::checkRequirements(
			thx::registry()->serviceManager(), reqs);
	}

	std::vector<PluginInfo> plugins()
	{
		return thx::registry()->pluginManager().plugins();
	}

	std::vector<PluginInfo> plugins(State state)
	{
		return thx::registry()->pluginManager().plugins(state);
	}

	std::optional<PluginInfo> pluginInfo(const std::string& path)
	{
		return thx::registry()->pluginManager().pluginInfo(path);
	}

	bool is(State state, const std::string& path)
	{
		return thx::registry()->pluginManager().is(state, path);
	}

	bool isDiscovered(const std::string& path)
	{
		return thx::registry()->pluginManager().isDiscovered(path);
	}

	bool isOpened(const std::string& path)
	{
		return thx::registry()->pluginManager().isOpened(path);
	}

	bool isLoaded(const std::string& path)
	{
		return thx::registry()->pluginManager().isLoaded(path);
	}

	std::vector<PluginInfo> pluginsProviding(const std::string& serviceId)
	{
		return thx::registry()->pluginManager().pluginsProviding(serviceId);
	}

	std::optional<PluginInfo> pluginByName(const std::string& name)
	{
		return thx::registry()->pluginManager().pluginByName(name);
	}

	LoadSummary loadWithDependencies(const std::string& path)
	{
		return thx::registry()->pluginManager().loadWithDependencies(path);
	}

	LoadSummary loadAll(Span<const PluginInfo> plugins)
	{
		return thx::registry()->pluginManager().loadAll(plugins);
	}

	std::size_t collectGarbage() noexcept
	{
		return thx::registry()->pluginGarbage().collect();
	}

	std::size_t pendingGarbage() noexcept
	{
		return thx::registry()->pluginGarbage().pending();
	}

} // namespace thx::plugin
