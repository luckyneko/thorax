/*
 *  Created by LuckyNeko on 25/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "interfaces/file_service.h"
#include <thx/platform.h>

#include <cstddef>
#include <cstdio>

namespace
{

struct FileServiceImpl : examples::IFileService
{
	int read(const char* path, char* buffer, int buffer_size) override
	{
		if (!path || !buffer || buffer_size <= 0)
			return -1;

		FILE* f = std::fopen(path, "rb");
		if (!f)
			return -1;

		auto n = static_cast<int>(
			std::fread(buffer, 1, static_cast<std::size_t>(buffer_size - 1), f));
		std::fclose(f);
		return n;
	}
};

} // namespace

THX_DEFINE_PLUGIN(FileServiceImpl)
