/*
 *  Created by LuckyNeko on 25/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include <thx/service/iservice.h>

namespace examples
{

	// Shared interface header — included by both the file plugin and the host.
	struct IFileService : thx::service::Service<IFileService>
	{
		static constexpr thx::Version staticVersion()
		{
			return thx::Version{1, 0, 0};
		}

		// Reads up to (buffer_size - 1) bytes from path into buffer.
		// Returns the number of bytes read, or -1 on failure.
		virtual int read(const char* path, char* buffer, int buffer_size) = 0;
	};

} // namespace examples
