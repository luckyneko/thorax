/*
 *  Created by LuckyNeko on 01/06/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "thx/io/mode.h"
#include "thx/io/protocol.h"
#include "thx/io/stream.h"
#include "thx/result.h"
#include "thx/string_view.h"
#include "thx/thx_api.h"

#include <memory>

// The streaming I/O facade. Open an address (local path, http://…, …) in read
// or write mode and get back a StreamHandle. These forward to the registered
// thx::io::IIOService (see io_service.h), auto-provisioning a default service
// with a built-in file:// handler on first use — so file streaming works with
// zero plugins, and a handler plugin's onLoad can simply call addHandler().
namespace thx::io
{
	// Open `address` in `mode`. The address is "scheme://rest" (e.g.
	// "file:///tmp/x", "http://host/path"); a bare path with no scheme is
	// treated as a local file. Returns an owning StreamHandle or an Error.
	THX_API Result<StreamHandle, Error> open(StringView address, Mode mode = Mode::Read);

	// Contribute a scheme handler to the I/O service (auto-provisioning it if
	// needed). Returns false if a scheme it reports is already served.
	THX_API bool addHandler(std::shared_ptr<IProtocol> handler);

	// Remove a previously-added handler by raw-pointer identity.
	THX_API void removeHandler(IProtocol* handler);

} // namespace thx::io
