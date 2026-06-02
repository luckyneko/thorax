/*
 *  Created by LuckyNeko on 02/06/2026.
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
#include "thx/service/iservice.h"
#include "thx/string_view.h"
#include "thx/version_type.h"

#include <memory>

namespace thx::io
{
	// The streaming-I/O dispatcher, exposed as an ordinary thorax service.
	//
	// io is NOT part of libthorax core — it is built on the public API like any
	// other consumer (see CLAUDE.md "Scope & inclusion criteria"). A single
	// provider plugin (plugins/io/service) registers one IIoService under the
	// stable id "thx.io.IIoService"; the io facade (thx::io::open / addHandler /
	// removeHandler in io.h) resolves it with getService<IIoService>(). Handler
	// plugins (file, http) declare it as a requirement and contribute scheme
	// handlers via addHandler().
	//
	// Beyond a plain service this carries the contributor sub-pattern: the
	// dispatcher holds its IProtocol handlers weakly and routes open() to the
	// one serving the address's scheme. Only ABI-stable types cross the vtable
	// (StringView, Mode, the shared_ptr/raw-pointer handshake, StreamHandle), so
	// the provider and the handlers may live in different DSOs.
	class IIoService : public thx::service::Service<IIoService>
	{
	public:
		static constexpr thx::Version staticVersion() { return thx::Version{1, 0, 0}; }

		// Open `address` (scheme://rest; a bare path means file) in `mode`,
		// routing to the handler registered for its scheme. Returns an owning
		// StreamHandle or an Error (NoHandler when no handler serves the scheme).
		virtual Result<StreamHandle, Error> open(StringView address, Mode mode) = 0;

		// Contribute a scheme handler (held weakly; evicted when the contributing
		// plugin drops its shared_ptr). Returns false only when `handler` is null.
		virtual bool addHandler(std::shared_ptr<IProtocol> handler) = 0;

		// Remove a previously-added handler by raw-pointer identity.
		virtual void removeHandler(IProtocol* handler) = 0;
	};

} // namespace thx::io
