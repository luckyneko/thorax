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

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace thx::io
{
	// The I/O dispatcher. Holds the registered IProtocol handlers (weak_ptr —
	// evicted when their owning plugin drops its shared_ptr) indexed by scheme,
	// and routes open() to the handler serving the address's scheme. Owns a
	// built-in file:// handler outright, so file streaming works before any
	// plugin loads.
	//
	// Unlike logging's ILogService, this is NOT a registered ServiceManager
	// service: the dispatcher is shared core infrastructure (a contributor
	// pattern with many handlers), not something a plugin "provides". Making it
	// a service would let a handler plugin's onLoad-time addHandler() register a
	// service that finalizeLoad mis-attributes against the plugin's manifest.
	// Instead the Registry owns one IoService by value, reached via the
	// thx::io::* facade.
	//
	// Thread-safe: a coarse mutex guards the scheme index; open() releases it
	// before dispatching so a handler may call back in.
	class IoService
	{
	public:
		IoService();

		IoService(IoService const&) = delete;
		IoService& operator=(IoService const&) = delete;

		Result<StreamHandle, Error> open(StringView address, Mode mode);
		void addHandler(std::shared_ptr<IProtocol> handler);
		void removeHandler(IProtocol* handler);

		// Drop all contributed handlers and re-index the built-ins (file). Used
		// by thx::shutdown() to return the subsystem to its initial state.
		void clear();

	private:
		// Index every scheme a handler reports → the handler (caller holds lock).
		void indexLocked(std::shared_ptr<IProtocol> const& handler);

		std::mutex m_mutex;
		// scheme -> handler (weak: contributed handlers live as long as their
		// plugin; built-ins are kept alive by m_builtins).
		std::unordered_map<std::string, std::weak_ptr<IProtocol>> m_byScheme;
		std::vector<std::shared_ptr<IProtocol>> m_builtins;
	};

} // namespace thx::io
