/*
 *  Created by LuckyNeko on 25/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "interfaces/file_service.h"
#include <thx/platform.h>

#include <fstream>

namespace
{

struct FileServiceImpl : examples::IFileService
{
	int read(const char* path, char* buffer, int buffer_size) override
	{
		if (!path || !buffer || buffer_size <= 0)
			return -1;

		std::ifstream f(path, std::ios::binary);
		if (!f)
			return -1;

		f.read(buffer, static_cast<std::streamsize>(buffer_size - 1));
		return static_cast<int>(f.gcount());
	}
};

} // namespace

THX_DEFINE_SERVICE_PLUGIN(FileServiceImpl)
