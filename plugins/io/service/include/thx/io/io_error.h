/*
 *  Created by LuckyNeko on 02/06/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include <string>

// io's own error domain. io is not part of libthorax core, so it does not draw
// on core's thx::ErrorCode — it carries thx::io::Error, returned as
// Result<T, thx::io::Error> (Result<T, E> is generic over the error type). This
// keeps core's ErrorCode confined to framework-machinery codes.
namespace thx::io
{
	enum class ErrorCode
	{
		Unknown = 0,
		NoHandler,	 // no handler registered for the address's scheme (or no IIoService)
		OpenFailed,	 // the underlying resource could not be opened
		Unsupported, // operation/mode not supported by the handler (e.g. writing a read-only stream)
		IoError,	 // a read/write/seek/transfer operation failed
	};

	// Lightweight error descriptor returned (not thrown) by io operations.
	struct Error
	{
		ErrorCode code{ErrorCode::Unknown};
		std::string message;
	};

} // namespace thx::io
