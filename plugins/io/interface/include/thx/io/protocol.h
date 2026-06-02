/*
 *  Created by LuckyNeko on 01/06/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "thx/io/io_error.h"
#include "thx/io/mode.h"
#include "thx/io/stream.h"
#include "thx/result.h"
#include "thx/span.h"
#include "thx/string_view.h"

namespace thx::io
{
	// A scheme handler — implement this to teach the I/O subsystem how to open a
	// family of addresses (file, http, s3, …). Register it with
	// thx::io::addHandler(); the IIOService keys it by the scheme(s) it reports
	// and dispatches open() by the scheme of the requested address.
	//
	// Contributed by value of a std::shared_ptr; the IIOService holds a weak_ptr
	// and evicts the handler automatically once the contributing plugin drops
	// its shared_ptr (same model as the logging backends).
	class IProtocol
	{
	public:
		virtual ~IProtocol() = default;

		// The URI scheme(s) this handler serves, without "://" (e.g. {"http"}).
		// The returned view must stay valid for the handler's lifetime; the
		// IIOService copies the scheme strings into its index at registration.
		virtual Span<const StringView> schemes() const = 0;

		// Open `address` (the full "scheme://rest" string) in `mode`. Returns an
		// owning StreamHandle on success, or an Error (e.g. unsupported mode).
		virtual Result<StreamHandle, Error> open(StringView address, Mode mode) = 0;
	};

} // namespace thx::io
