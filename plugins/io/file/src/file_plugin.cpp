/*
 *  Created by LuckyNeko on 02/06/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

// The built-in file:// handler, as a handler plugin. It requires the io
// provider (IIoService) and contributes a file:// IProtocol to it via
// addHandler() — the contributor pattern, registering no service of its own —
// so once both are loaded thx::io::open("file://…" or a bare path) routes here.

#include <thx/io/io.h>
#include <thx/io/io_service.h>
#include <thx/io/protocol.h>
#include <thx/io/stream.h>
#include <thx/plugin/iplugin.h>
#include <thx/plugin/platform.h>

#include <fstream>
#include <ios>
#include <memory>
#include <string>

namespace
{
	using namespace thx;	 // Result, Error, ErrorCode, Span, StringView, Version
	using namespace thx::io; // IStream, IProtocol, StreamHandle, Mode, Whence

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

		Result<StreamHandle, thx::io::Error> open(StringView address, Mode mode) override
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
				return Result<StreamHandle, thx::io::Error>::err(
					{thx::io::ErrorCode::OpenFailed, "file: cannot open '" + path + "'"});

			return Result<StreamHandle, thx::io::Error>::ok(
				StreamHandle(new FileStream(std::move(file), canRead, canWrite)));
		}
	};

	class FilePlugin : public thx::plugin::IPlugin
	{
	public:
		thx::StringView name() const override { return "thx.io.FileProtocol"; }
		thx::Version version() const override { return thx::Version{1, 0, 0}; }

		thx::Span<const thx::plugin::ServiceRequirement> required() const override
		{
			static const thx::plugin::ServiceRequirement kReqs[] = {
				{thx::io::IIoService::staticId(), thx::io::IIoService::staticVersion()}};
			return {kReqs, 1};
		}

		bool onLoad() override
		{
			m_protocol = std::make_shared<FileProtocol>();
			return thx::io::addHandler(m_protocol);
		}

		void onUnload() override
		{
			if (m_protocol)
				thx::io::removeHandler(m_protocol.get());
			m_protocol.reset();
		}

	private:
		std::shared_ptr<FileProtocol> m_protocol;
	};

} // namespace

THX_DEFINE_PLUGIN(FilePlugin)
