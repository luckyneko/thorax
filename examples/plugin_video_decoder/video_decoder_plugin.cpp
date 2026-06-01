/*
 *  Created by LuckyNeko on 01/06/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

// A second decoder contributor — for video. Identical in shape to the image
// decoder plugin: a custom IPlugin that requires IAssetService and adds its
// decoder in onLoad. Having two independent decoder DSOs is what lets a host
// "load every decoder in a directory" and have the asset service dispatch to
// whichever one recognises a given file.

#include "interfaces/asset_service.h"

#include <thx/log/log.h>
#include <thx/plugin/platform.h>
#include <thx/service/service.h>

#include <cstring>
#include <memory>
#include <string>

namespace
{

	bool endsWith(const char* path, const char* ext)
	{
		if (!path)
			return false;
		std::size_t lp = std::strlen(path), le = std::strlen(ext);
		return lp >= le && std::strcmp(path + (lp - le), ext) == 0;
	}

	struct VideoDecoder : examples::IAssetDecoder
	{
		bool canDecode(const char* path) override
		{
			return endsWith(path, ".mp4") || endsWith(path, ".mov");
		}

		bool decode(const char* path, examples::AssetInfo& out) override
		{
			out.kind = examples::AssetKind::Video;
			out.width = 1280;
			out.height = 720;
			out.durationMs = 12000; // pretend every clip is 12s
			thx::log::info(std::string("VideoDecoder: decoded '") + path + "'");
			return true;
		}
	};

	class VideoDecoderPlugin : public thx::plugin::IPlugin
	{
	public:
		thx::StringView name() const override { return "examples.media.VideoDecoder"; }
		thx::Version version() const override { return thx::Version{1, 0, 0}; }

		bool onLoad() override
		{
			auto media = thx::service::getService<examples::IAssetService>();
			if (!media)
			{
				thx::log::error("VideoDecoder: IAssetService not available at load");
				return false;
			}
			m_decoder = std::make_shared<VideoDecoder>();
			media->addDecoder(m_decoder);
			thx::log::info("VideoDecoder: registered (.mp4/.mov)");
			return true;
		}

		void onUnload() override
		{
			if (auto media = thx::service::getService<examples::IAssetService>(); media && m_decoder)
				media->removeDecoder(m_decoder.get());
			m_decoder.reset();
		}

		thx::Span<const thx::plugin::ServiceRequirement> required() const override
		{
			static const thx::plugin::ServiceRequirement kRequires[] = {
				{examples::IAssetService::staticId(), examples::IAssetService::staticVersion()},
			};
			return {kRequires, 1};
		}

	private:
		std::shared_ptr<VideoDecoder> m_decoder;
	};

} // namespace

THX_DEFINE_PLUGIN(VideoDecoderPlugin)
