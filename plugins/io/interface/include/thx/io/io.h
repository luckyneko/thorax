/*
 *  Created by LuckyNeko on 01/06/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "thx/io/io_error.h"
#include "thx/io/io_service.h"
#include "thx/io/mode.h"
#include "thx/io/protocol.h"
#include "thx/io/stream.h"
#include "thx/result.h"
#include "thx/service/service.h"
#include "thx/string_view.h"

#include <memory>

// The streaming I/O facade. Open an address (local path, http://…, …) in read
// or write mode and get back a StreamHandle.
//
// These are header-only inline shims over the registered IIoService — io is not
// part of libthorax, so there is no exported symbol here. Load the io provider
// plugin (plugins/io/service) plus the handler plugins you need (file, http) to
// make them work; with no provider registered, open() returns NoHandler. The
// IIoService handle is held only for the duration of each call (never across
// calls), so the provider plugin can be unloaded with no dangling reference.
namespace thx::io
{
	// Open `address` in `mode`. The address is "scheme://rest" (e.g.
	// "file:///tmp/x", "http://host/path"); a bare path with no scheme is
	// treated as a local file. Returns an owning StreamHandle or an Error.
	inline Result<StreamHandle, Error> open(StringView address, Mode mode = Mode::Read)
	{
		auto svc = thx::service::getService<IIoService>();
		if (!svc)
			return Result<StreamHandle, Error>::err(
				{ErrorCode::NoHandler, "io: no IIoService registered (load the io provider plugin)"});
		return svc->open(address, mode);
	}

	// Contribute a scheme handler to the I/O service. Returns false if no
	// IIoService is registered or `handler` is null.
	inline bool addHandler(std::shared_ptr<IProtocol> handler)
	{
		auto svc = thx::service::getService<IIoService>();
		if (!svc)
			return false;
		return svc->addHandler(std::move(handler));
	}

	// Remove a previously-added handler by raw-pointer identity. No-op if no
	// IIoService is registered.
	inline void removeHandler(IProtocol* handler)
	{
		if (auto svc = thx::service::getService<IIoService>())
			svc->removeHandler(handler);
	}

} // namespace thx::io
