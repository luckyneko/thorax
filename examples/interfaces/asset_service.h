/*
 *  Created by LuckyNeko on 01/06/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include <thx/service/iservice.h>

#include <memory>

// The media-asset example domain. One service, IAssetService, decodes an asset
// by dispatching to registered IAssetDecoder *contributors* — the provider /
// contribution pattern (the thx::io subsystem uses the same shape: an
// IProtocol per scheme). A "core" plugin registers IAssetService; decoder plugins do not
// register a service of their own — they look the service up and call
// addDecoder(), so each format handler is a separate, independently-loadable
// DSO. This header is shared by the core plugin, the decoder plugins, and hosts.
namespace examples
{

	enum class AssetKind
	{
		Unknown,
		Image,
		Video,
	};

	// Result of a successful decode. POD with a fixed layout so it crosses the
	// IAssetDecoder / IAssetService virtual boundary by value safely. Fields not
	// relevant to a kind are left zero (e.g. durationMs is 0 for an image).
	struct AssetInfo
	{
		AssetKind kind = AssetKind::Unknown;
		int width = 0;
		int height = 0;
		int durationMs = 0;
	};

	// Provider interface. Implement this to contribute support for one or more
	// asset formats. IAssetService holds only a std::weak_ptr<IAssetDecoder>;
	// when the contributing plugin drops its shared_ptr the decoder is evicted
	// automatically on the next decode()/addDecoder().
	class IAssetDecoder
	{
	public:
		virtual ~IAssetDecoder() = default;

		// Return true if this decoder recognises the asset at `path` (typically
		// by extension). Probed in registration order; the first to accept wins.
		virtual bool canDecode(const char* path) = 0;

		// Decode `path` into `out`. Returns true on success.
		virtual bool decode(const char* path, AssetInfo& out) = 0;
	};

	// Main asset service. Include this header in any plugin or host that wants to
	// decode assets or contribute a decoder.
	//
	// Usage (host):
	//   auto media = thx::service::getService<IAssetService>();
	//   examples::AssetInfo info;
	//   if (media->decode("photo.png", info)) { ... }
	//
	// Usage (decoder plugin, in onLoad):
	//   auto media = thx::service::getService<IAssetService>();
	//   media->addDecoder(std::make_shared<PngDecoder>());
	class IAssetService : public thx::service::Service<IAssetService>
	{
	public:
		static constexpr thx::Version staticVersion()
		{
			return thx::Version{1, 0, 0};
		}

		// Decode `path` using the first registered decoder whose canDecode() is
		// true. Returns false if no decoder accepts the path or the decode fails.
		virtual bool decode(const char* path, AssetInfo& out) = 0;

		// Register a decoder; the service stores a weak_ptr. Decoders are probed
		// in the order they were added — register specific formats before generic
		// fallbacks.
		virtual void addDecoder(std::shared_ptr<IAssetDecoder> decoder) = 0;

		// Eagerly remove a decoder by raw-pointer identity before it expires.
		virtual void removeDecoder(IAssetDecoder* decoder) = 0;
	};

} // namespace examples
