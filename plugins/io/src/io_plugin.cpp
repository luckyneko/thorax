/*
 *  Created by LuckyNeko on 25/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "thx/plugins/io/io_service.h"
#include <thx/plugin/platform.h>

#include <algorithm>
#include <fstream>
#include <mutex>
#include <vector>

using namespace thx::plugins::io;

namespace
{

// ---------------------------------------------------------------------------
// TextReader — generic fallback reader; accepts any path
// ---------------------------------------------------------------------------

struct TextReader : IFileReader
{
	bool canRead(const char* /*path*/) override { return true; }

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

// ---------------------------------------------------------------------------
// IOServiceImpl
// ---------------------------------------------------------------------------

struct IOServiceImpl : IIOService
{
	std::mutex                                m_mutex;
	std::vector<std::weak_ptr<IFileReader>>   m_readers;

	int read(const char* path, char* buffer, int buffer_size) override
	{
		// Promote live weak_ptrs under the lock; call read() outside to avoid
		// deadlock if a reader calls back into the service.
		std::vector<std::shared_ptr<IFileReader>> live;
		{
			std::lock_guard lock(m_mutex);
			auto it = m_readers.begin();
			while (it != m_readers.end())
			{
				if (auto r = it->lock())
				{
					live.push_back(std::move(r));
					++it;
				}
				else
				{
					it = m_readers.erase(it); // cull expired
				}
			}
		}

		for (auto& r : live)
		{
			if (r->canRead(path))
			{
				int result = r->read(path, buffer, buffer_size);
				return (result < 0) ? -2 : result;
			}
		}
		return -1; // no reader accepted
	}

	void addReader(std::shared_ptr<IFileReader> reader) override
	{
		if (!reader)
			return;
		std::lock_guard lock(m_mutex);
		m_readers.push_back(std::move(reader));
	}

	void removeReader(IFileReader* key) override
	{
		std::lock_guard lock(m_mutex);
		m_readers.erase(
			std::remove_if(m_readers.begin(), m_readers.end(),
				[key](std::weak_ptr<IFileReader> const& wp)
				{
					auto sp = wp.lock();
					return !sp || sp.get() == key;
				}),
			m_readers.end());
	}

	std::shared_ptr<IFileReader> makeTextReader() override
	{
		return std::make_shared<TextReader>();
	}
};

} // namespace

THX_DEFINE_SERVICE_PLUGIN(IOServiceImpl)
