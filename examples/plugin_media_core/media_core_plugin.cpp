/*
 *  Created by LuckyNeko on 01/06/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

// The media "core": registers IAssetService, the decode dispatcher every
// decoder plugin contributes to. This is the common one-service-per-DSO shape,
// so it uses THX_DEFINE_SERVICE_PLUGIN — the shim registers the service in
// onLoad and unregisters it in onUnload with no hand-written IPlugin.
//
// The decoder registry holds weak_ptrs: a decoder lives only as long as the
// plugin that contributed it, and is evicted automatically once that plugin
// unloads and drops its shared_ptr. (Same pattern as the in-tree io service.)

#include "interfaces/asset_service.h"

#include <thx/log.h>
#include <thx/plugin/platform.h>

#include <algorithm>
#include <mutex>
#include <vector>

namespace
{

	struct AssetServiceImpl : examples::IAssetService
	{
		std::mutex m_mutex;
		std::vector<std::weak_ptr<examples::IAssetDecoder>> m_decoders;

		bool decode(const char* path, examples::AssetInfo& out) override
		{
			if (!path)
				return false;

			// Promote live decoders under the lock, probe without holding it so a
			// decoder may safely call back into the service.
			std::vector<std::shared_ptr<examples::IAssetDecoder>> live;
			{
				std::lock_guard lock(m_mutex);
				auto it = m_decoders.begin();
				while (it != m_decoders.end())
				{
					if (auto d = it->lock())
					{
						live.push_back(std::move(d));
						++it;
					}
					else
					{
						it = m_decoders.erase(it); // cull expired
					}
				}
			}

			for (auto& d : live)
			{
				if (d->canDecode(path) && d->decode(path, out))
					return true;
			}
			thx::logMessage(thx::LogLevel::Warn, std::string("IAssetService: no decoder accepted '") + path + "'");
			return false;
		}

		void addDecoder(std::shared_ptr<examples::IAssetDecoder> decoder) override
		{
			if (!decoder)
				return;
			std::lock_guard lock(m_mutex);
			m_decoders.push_back(std::move(decoder));
		}

		void removeDecoder(examples::IAssetDecoder* key) override
		{
			std::lock_guard lock(m_mutex);
			m_decoders.erase(
				std::remove_if(m_decoders.begin(), m_decoders.end(),
							   [key](const std::weak_ptr<examples::IAssetDecoder>& wp)
							   {
								   auto sp = wp.lock();
								   return !sp || sp.get() == key;
							   }),
				m_decoders.end());
		}
	};

} // namespace

THX_DEFINE_SERVICE_PLUGIN(AssetServiceImpl)
