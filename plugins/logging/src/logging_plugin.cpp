/*
 *  Created by LuckyNeko on 25/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "thx/plugins/logging/logging_service.h"
#include <thx/plugin/platform.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

using namespace thx::plugins::logging;

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

namespace
{

const char* levelTag(LogLevel l) noexcept
{
	switch (l)
	{
		case LogLevel::Debug: return "DBG";
		case LogLevel::Info:  return "INF";
		case LogLevel::Warn:  return "WRN";
		case LogLevel::Error: return "ERR";
		default:              return "???";
	}
}

// ---------------------------------------------------------------------------
// ConsoleSink — writes to stderr
// ---------------------------------------------------------------------------

struct ConsoleSink : ILogBackend
{
	void write(LogLevel level, const char* message) override
	{
		std::fprintf(stderr, "[%s] %s\n", levelTag(level), message);
		std::fflush(stderr);
	}
};

// ---------------------------------------------------------------------------
// FileSink — appends to a file; no rotation
// ---------------------------------------------------------------------------

struct FileSink : ILogBackend
{
	std::ofstream m_file;

	explicit FileSink(const char* path) { m_file.open(path, std::ios::app); }

	void write(LogLevel level, const char* message) override
	{
		if (!m_file)
			return;
		m_file << "[" << levelTag(level) << "] " << message << "\n";
		m_file.flush();
	}
};

// ---------------------------------------------------------------------------
// RotatingFileSink — rotates when the active file exceeds max_size_bytes
// ---------------------------------------------------------------------------
//
// Rotation renames existing numbered files outward then opens a fresh base file:
//   base      → base.1
//   base.1    → base.2
//   ...
//   base.N-1  → base.N   (base.N, if present, is silently overwritten)

struct RotatingFileSink : ILogBackend
{
	std::string   m_path;
	long          m_maxSize{0};
	int           m_maxFiles{0};
	std::ofstream m_file;
	long          m_currentSize{0};

	RotatingFileSink(const char* path, int max_size_bytes, int max_files)
		: m_path(path)
		, m_maxSize(static_cast<long>(max_size_bytes))
		, m_maxFiles(max_files)
	{
		m_file.open(path, std::ios::app);
		if (m_file)
		{
			auto pos = m_file.tellp();
			if (pos >= 0)
				m_currentSize = static_cast<long>(pos);
		}
	}

	void rotate()
	{
		m_file.close();

		// Shift numbered files outward (high → low index to avoid overwrite).
		for (int i = m_maxFiles; i > 1; --i)
		{
			auto from = m_path + "." + std::to_string(i - 1);
			auto to   = m_path + "." + std::to_string(i);
			std::rename(from.c_str(), to.c_str());
		}
		// Base file → .1
		if (m_maxFiles >= 1)
			std::rename(m_path.c_str(), (m_path + ".1").c_str());

		m_file.open(m_path, std::ios::trunc);
		if (!m_file)
		{
			// Rotation renamed away the base file but couldn't create a fresh one.
			// Best-effort: try to promote .1 back to base and reopen for append.
			std::rename((m_path + ".1").c_str(), m_path.c_str());
			m_file.open(m_path, std::ios::app);
		}
		m_currentSize = 0;
	}

	void write(LogLevel level, const char* message) override
	{
		if (!m_file)
			return;

		if (m_currentSize > m_maxSize)
			rotate();

		if (!m_file)
			return;

		auto line = std::string("[") + levelTag(level) + "] " + message + "\n";
		m_file << line;
		m_file.flush();
		if (m_file)
			m_currentSize += static_cast<long>(line.size());
	}
};

// ---------------------------------------------------------------------------
// LoggingServiceImpl
// ---------------------------------------------------------------------------

struct LoggingServiceImpl : ILoggingService
{
	std::mutex                                m_mutex;
	std::vector<std::weak_ptr<ILogBackend>>   m_backends;

	void log(LogLevel level, const char* message) override
	{
		if (!message)
			message = "";
		// Promote live weak_ptrs while holding the lock; write without holding it
		// so that backend::write() can call back into the service safely.
		std::vector<std::shared_ptr<ILogBackend>> live;
		{
			std::lock_guard lock(m_mutex);
			auto it = m_backends.begin();
			while (it != m_backends.end())
			{
				if (auto b = it->lock())
				{
					live.push_back(std::move(b));
					++it;
				}
				else
				{
					it = m_backends.erase(it); // cull expired
				}
			}
		}
		for (auto& b : live)
			b->write(level, message);
	}

	void addBackend(std::shared_ptr<ILogBackend> backend) override
	{
		if (!backend)
			return;
		std::lock_guard lock(m_mutex);
		m_backends.push_back(std::move(backend));
	}

	void removeBackend(ILogBackend* key) override
	{
		std::lock_guard lock(m_mutex);
		m_backends.erase(
			std::remove_if(m_backends.begin(), m_backends.end(),
				[key](std::weak_ptr<ILogBackend> const& wp)
				{
					auto sp = wp.lock();
					return !sp || sp.get() == key;
				}),
			m_backends.end());
	}

	std::shared_ptr<ILogBackend> makeConsoleBackend() override
	{
		return std::make_shared<ConsoleSink>();
	}

	std::shared_ptr<ILogBackend> makeFileBackend(const char* path) override
	{
		if (!path)
			return nullptr;
		auto sink = std::make_shared<FileSink>(path);
		return sink->m_file ? std::shared_ptr<ILogBackend>(std::move(sink)) : nullptr;
	}

	std::shared_ptr<ILogBackend> makeRotatingFileBackend(
		const char* path, int max_size_bytes, int max_files) override
	{
		if (!path || max_size_bytes <= 0 || max_files <= 0)
			return nullptr;
		auto sink = std::make_shared<RotatingFileSink>(path, max_size_bytes, max_files);
		return sink->m_file ? std::shared_ptr<ILogBackend>(std::move(sink)) : nullptr;
	}
};

} // namespace

THX_DEFINE_SERVICE_PLUGIN(LoggingServiceImpl)
