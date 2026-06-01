/*
 *  Created by LuckyNeko on 01/06/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "io/io_service.h"

#include "thx/log/log.h"

#include <fstream>
#include <ios>
#include <string>

namespace thx::io
{

	namespace
	{
		// The scheme of "scheme://rest"; a bare path with no "://" is "file".
		std::string schemeOf(StringView address)
		{
			std::string s(address.data(), address.size());
			auto pos = s.find("://");
			if (pos == std::string::npos)
				return "file";
			return s.substr(0, pos);
		}

		// ----- built-in file handler ---------------------------------------

		class FileStream : public IStream
		{
		public:
			FileStream(std::fstream file, bool canRead, bool canWrite)
				: m_file(std::move(file))
				, m_read(canRead)
				, m_write(canWrite)
			{
			}

			std::int64_t read(Span<std::uint8_t> buffer) override
			{
				if (!m_read)
					return -1;
				m_file.read(reinterpret_cast<char*>(buffer.data()),
							static_cast<std::streamsize>(buffer.size()));
				auto got = m_file.gcount();
				if (m_file.eof())
					m_file.clear(); // so subsequent reads cleanly return 0
				return static_cast<std::int64_t>(got);
			}

			std::int64_t write(Span<const std::uint8_t> data) override
			{
				if (!m_write)
					return -1;
				m_file.write(reinterpret_cast<const char*>(data.data()),
							 static_cast<std::streamsize>(data.size()));
				if (!m_file)
					return -1;
				return static_cast<std::int64_t>(data.size());
			}

			std::int64_t seek(std::int64_t offset, Whence whence) override
			{
				std::ios_base::seekdir dir = whence == Whence::Begin	 ? std::ios::beg
											 : whence == Whence::Current ? std::ios::cur
																		 : std::ios::end;
				if (m_read)
					m_file.seekg(static_cast<std::streamoff>(offset), dir);
				if (m_write)
					m_file.seekp(static_cast<std::streamoff>(offset), dir);
				if (!m_file)
					return -1;
				return tell();
			}

			std::int64_t tell() const override
			{
				auto& f = const_cast<std::fstream&>(m_file);
				auto pos = m_read ? f.tellg() : f.tellp();
				return pos < 0 ? -1 : static_cast<std::int64_t>(pos);
			}

			bool canRead() const override { return m_read; }
			bool canWrite() const override { return m_write; }
			bool canSeek() const override { return true; }

		private:
			std::fstream m_file;
			bool m_read;
			bool m_write;
		};

		class FileProtocol : public IProtocol
		{
		public:
			Span<const StringView> schemes() const override
			{
				static const StringView kSchemes[] = {StringView("file")};
				return {kSchemes, 1};
			}

			Result<StreamHandle, Error> open(StringView address, Mode mode) override
			{
				// Strip a leading "file://" if present; otherwise treat the whole
				// address as a local path.
				std::string addr(address.data(), address.size());
				const std::string prefix = "file://";
				std::string path = addr.compare(0, prefix.size(), prefix) == 0
									   ? addr.substr(prefix.size())
									   : addr;

				bool canRead = false, canWrite = false;
				std::ios_base::openmode flags = std::ios::binary;
				switch (mode)
				{
					case Mode::Read:
						flags |= std::ios::in;
						canRead = true;
						break;
					case Mode::Write:
						flags |= std::ios::out | std::ios::trunc;
						canWrite = true;
						break;
					case Mode::ReadWrite:
						flags |= std::ios::in | std::ios::out;
						canRead = canWrite = true;
						break;
					case Mode::Append:
						flags |= std::ios::out | std::ios::app;
						canWrite = true;
						break;
				}

				std::fstream file(path, flags);
				if (!file.is_open())
					return Result<StreamHandle, Error>::err(
						{ErrorCode::OpenFailed, "file: cannot open '" + path + "'"});

				return Result<StreamHandle, Error>::ok(
					StreamHandle(new FileStream(std::move(file), canRead, canWrite)));
			}
		};

	} // namespace

	IoService::IoService()
	{
		auto file = std::make_shared<FileProtocol>();
		m_builtins.push_back(file);
		std::lock_guard lock(m_mutex);
		indexLocked(file);
	}

	void IoService::indexLocked(std::shared_ptr<IProtocol> const& handler)
	{
		for (auto scheme : handler->schemes())
			m_byScheme[std::string(scheme.data(), scheme.size())] = handler;
	}

	void IoService::addHandler(std::shared_ptr<IProtocol> handler)
	{
		if (!handler)
			return;
		std::lock_guard lock(m_mutex);
		for (auto scheme : handler->schemes())
		{
			std::string key(scheme.data(), scheme.size());
			auto it = m_byScheme.find(key);
			if (it != m_byScheme.end() && !it->second.expired())
			{
				thx::log::warn("io: scheme '" + key + "' already has a handler; ignoring duplicate");
				continue;
			}
			m_byScheme[key] = handler;
		}
	}

	void IoService::removeHandler(IProtocol* handler)
	{
		std::lock_guard lock(m_mutex);
		for (auto it = m_byScheme.begin(); it != m_byScheme.end();)
		{
			auto sp = it->second.lock();
			if (!sp || sp.get() == handler)
				it = m_byScheme.erase(it);
			else
				++it;
		}
	}

	void IoService::clear()
	{
		std::lock_guard lock(m_mutex);
		m_byScheme.clear();
		for (auto const& b : m_builtins)
			indexLocked(b);
	}

	Result<StreamHandle, Error> IoService::open(StringView address, Mode mode)
	{
		std::string scheme = schemeOf(address);

		std::shared_ptr<IProtocol> handler;
		{
			std::lock_guard lock(m_mutex);
			auto it = m_byScheme.find(scheme);
			if (it != m_byScheme.end())
			{
				handler = it->second.lock();
				if (!handler)
					m_byScheme.erase(it); // cull expired
			}
		}

		if (!handler)
			return Result<StreamHandle, Error>::err(
				{ErrorCode::NoHandler, "no I/O handler registered for scheme '" + scheme + "'"});

		// Dispatch without the lock held so a handler may call back in.
		return handler->open(address, mode);
	}

} // namespace thx::io
