/*
 *  Created by LuckyNeko on 25/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "thx/plugins/logging/logging_service.h"
#include <thx/platform.h>

#include <algorithm>
#include <cstdio>
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

const char* level_tag(LogLevel l) noexcept
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
		std::fprintf(stderr, "[%s] %s\n", level_tag(level), message);
		std::fflush(stderr);
	}
};

// ---------------------------------------------------------------------------
// FileSink — appends to a file; no rotation
// ---------------------------------------------------------------------------

struct FileSink : ILogBackend
{
	FILE* m_file{nullptr};

	explicit FileSink(const char* path) { m_file = std::fopen(path, "a"); }
	~FileSink() { if (m_file) std::fclose(m_file); }

	void write(LogLevel level, const char* message) override
	{
		if (!m_file)
			return;
		std::fprintf(m_file, "[%s] %s\n", level_tag(level), message);
		std::fflush(m_file);
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
	std::string m_path;
	long        m_max_size{0};
	int         m_max_files{0};
	FILE*       m_file{nullptr};
	long        m_current_size{0};

	RotatingFileSink(const char* path, int max_size_bytes, int max_files)
		: m_path(path)
		, m_max_size(static_cast<long>(max_size_bytes))
		, m_max_files(max_files)
	{
		m_file = std::fopen(path, "a");
		if (m_file)
		{
			std::fseek(m_file, 0, SEEK_END);
			long n = std::ftell(m_file);
			if (n >= 0)
				m_current_size = n;
		}
	}

	~RotatingFileSink() { if (m_file) std::fclose(m_file); }

	void rotate()
	{
		if (m_file)
		{
			std::fclose(m_file);
			m_file = nullptr;
		}

		// Shift numbered files outward (high → low index to avoid overwrite).
		for (int i = m_max_files; i > 1; --i)
		{
			auto from = m_path + "." + std::to_string(i - 1);
			auto to   = m_path + "." + std::to_string(i);
			std::rename(from.c_str(), to.c_str());
		}
		// Base file → .1
		if (m_max_files >= 1)
			std::rename(m_path.c_str(), (m_path + ".1").c_str());

		m_file = std::fopen(m_path.c_str(), "w");
		if (!m_file)
		{
			// Rotation renamed away the base file but couldn't create a fresh one.
			// Best-effort: try to promote .1 back to base and reopen for append.
			std::rename((m_path + ".1").c_str(), m_path.c_str());
			m_file = std::fopen(m_path.c_str(), "a");
		}
		m_current_size = 0;
	}

	void write(LogLevel level, const char* message) override
	{
		if (!m_file)
			return;

		if (m_current_size > m_max_size)
			rotate();

		if (!m_file)
			return;

		int n = std::fprintf(m_file, "[%s] %s\n", level_tag(level), message);
		if (n > 0)
			m_current_size += static_cast<long>(n);
		std::fflush(m_file);
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

	void add_backend(std::shared_ptr<ILogBackend> backend) override
	{
		if (!backend)
			return;
		std::lock_guard lock(m_mutex);
		m_backends.push_back(std::move(backend));
	}

	void remove_backend(ILogBackend* key) override
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

	std::shared_ptr<ILogBackend> make_console_backend() override
	{
		return std::make_shared<ConsoleSink>();
	}

	std::shared_ptr<ILogBackend> make_file_backend(const char* path) override
	{
		if (!path)
			return nullptr;
		auto sink = std::make_shared<FileSink>(path);
		return sink->m_file ? std::shared_ptr<ILogBackend>(std::move(sink)) : nullptr;
	}

	std::shared_ptr<ILogBackend> make_rotating_file_backend(
		const char* path, int max_size_bytes, int max_files) override
	{
		if (!path || max_size_bytes <= 0 || max_files <= 0)
			return nullptr;
		auto sink = std::make_shared<RotatingFileSink>(path, max_size_bytes, max_files);
		return sink->m_file ? std::shared_ptr<ILogBackend>(std::move(sink)) : nullptr;
	}
};

} // namespace

THX_DEFINE_PLUGIN(LoggingServiceImpl)
