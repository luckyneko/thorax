/*
 *  Created by LuckyNeko on 01/06/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

// An image decoder plugin. It registers NO service of its own — instead it
// *contributes* a decoder to the IAssetService provided by the media-core
// plugin. Because it needs IAssetService present before onLoad and has custom
// load logic, it uses the power-user THX_DEFINE_PLUGIN form (a hand-written
// IPlugin) rather than the one-service shim.
//
// The required() declaration means the loader refuses to load this plugin until
// IAssetService is registered — and thx::plugin::loadWithDependencies() will
// pull the media-core plugin in first automatically.

#include "interfaces/asset_service.h"

#include <thx/log.h>
#include <thx/plugin/platform.h>
#include <thx/service/service.h>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace
{

	bool endsWith(const char* path, const char* ext)
	{
		if (!path)
			return false;
		std::size_t lp = std::strlen(path), le = std::strlen(ext);
		return lp >= le && std::strcmp(path + (lp - le), ext) == 0;
	}

	// Handles PNG and JPEG. The "decode" is faked — a real decoder would parse
	// the header — but it shows the shape: recognise by extension, fill AssetInfo.
	struct ImageDecoder : examples::IAssetDecoder
	{
		bool canDecode(const char* path) override
		{
			return endsWith(path, ".png") || endsWith(path, ".jpg") || endsWith(path, ".jpeg");
		}

		bool decode(const char* path, examples::AssetInfo& out) override
		{
			out.kind = examples::AssetKind::Image;
			// Pretend PNGs are 1080p and JPEGs are 800x600.
			if (endsWith(path, ".png"))
			{
				out.width = 1920;
				out.height = 1080;
			}
			else
			{
				out.width = 800;
				out.height = 600;
			}
			out.durationMs = 0;
			thx::logMessage(thx::LogLevel::Info, std::string("ImageDecoder: decoded '") + path + "'");
			return true;
		}
	};

	class ImageDecoderPlugin : public thx::plugin::IPlugin
	{
	public:
		thx::StringView name() const override { return "examples.media.ImageDecoder"; }
		thx::Version version() const override { return thx::Version{1, 0, 0}; }

		bool onLoad() override
		{
			auto media = thx::service::getService<examples::IAssetService>();
			if (!media)
			{
				// required() guarantees this is present; guard defensively anyway.
				thx::logMessage(thx::LogLevel::Error, "ImageDecoder: IAssetService not available at load");
				return false;
			}
			m_decoder = std::make_shared<ImageDecoder>();
			media->addDecoder(m_decoder);
			thx::logMessage(thx::LogLevel::Info, "ImageDecoder: registered (.png/.jpg/.jpeg)");
			return true;
		}

		void onUnload() override
		{
			// Eagerly remove if the service is still around; dropping our
			// shared_ptr also lets the service evict the weak_ptr on its own.
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

		// Provides no service of its own — it extends IAssetService.

	private:
		std::shared_ptr<ImageDecoder> m_decoder;
	};

} // namespace

THX_DEFINE_PLUGIN(ImageDecoderPlugin)
