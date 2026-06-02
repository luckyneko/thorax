/*
 *  Created by LuckyNeko on 01/06/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

// An http:// handler for the thx::io subsystem, built on cpp-httplib. It
// requires the io provider (IIoService) and contributes an http:// IProtocol to
// it via addHandler() — registering no service of its own (the contributor
// pattern, like the media decoders) — so once both are loaded
// thx::io::open("http://…", Read) routes here. Read-only for now; write (PUT/
// POST) and https (TLS) are deferred.

#include <thx/io/io.h>
#include <thx/io/io_service.h>
#include <thx/io/protocol.h>
#include <thx/io/stream.h>
#include <thx/log/log.h>
#include <thx/plugin/iplugin.h>
#include <thx/plugin/platform.h>

#include <httplib.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>

namespace
{
	using namespace thx;	 // Result, Error, ErrorCode, Span, StringView, Version
	using namespace thx::io; // IStream, IProtocol, StreamHandle, Mode, Whence

	// An in-memory, seekable read stream over a fetched HTTP response body.
	// Qualify thx::io::IStream explicitly: on Windows the cpp-httplib include
	// pulls in the Windows SDK, whose global COM ::IStream would otherwise make
	// the unqualified name ambiguous against the one from `using namespace thx::io`.
	class HttpStream : public thx::io::IStream
	{
	public:
		explicit HttpStream(std::string body)
			: m_body(std::move(body))
		{
		}

		std::int64_t read(thx::Span<std::uint8_t> buffer) override
		{
			std::size_t remaining = m_body.size() - m_pos;
			std::size_t n = std::min(remaining, buffer.size());
			std::memcpy(buffer.data(), m_body.data() + m_pos, n);
			m_pos += n;
			return static_cast<std::int64_t>(n);
		}

		std::int64_t write(thx::Span<const std::uint8_t>) override { return -1; }

		std::int64_t seek(std::int64_t offset, Whence whence) override
		{
			std::int64_t base = whence == Whence::Begin		? 0
								: whence == Whence::Current ? static_cast<std::int64_t>(m_pos)
															: static_cast<std::int64_t>(m_body.size());
			std::int64_t target = base + offset;
			if (target < 0 || target > static_cast<std::int64_t>(m_body.size()))
				return -1;
			m_pos = static_cast<std::size_t>(target);
			return target;
		}

		std::int64_t tell() const override { return static_cast<std::int64_t>(m_pos); }

		bool canRead() const override { return true; }
		bool canWrite() const override { return false; }
		bool canSeek() const override { return true; }

	private:
		std::string m_body;
		std::size_t m_pos = 0;
	};

	class HttpProtocol : public IProtocol
	{
	public:
		thx::Span<const thx::StringView> schemes() const override
		{
			static const thx::StringView kSchemes[] = {thx::StringView("http")};
			return {kSchemes, 1};
		}

		Result<StreamHandle, thx::io::Error> open(thx::StringView address, Mode mode) override
		{
			if (mode != Mode::Read)
				return Result<StreamHandle, thx::io::Error>::err(
					{thx::io::ErrorCode::Unsupported, "http: only Read mode is supported"});

			// Split "http://host[:port]/path".
			std::string url(address.data(), address.size());
			const std::string prefix = "http://";
			if (url.compare(0, prefix.size(), prefix) != 0)
				return Result<StreamHandle, thx::io::Error>::err(
					{thx::io::ErrorCode::NoHandler, "http: address is not http://"});
			std::string rest = url.substr(prefix.size());

			auto slash = rest.find('/');
			std::string authority = rest.substr(0, slash);
			std::string path = slash == std::string::npos ? "/" : rest.substr(slash);

			std::string host = authority;
			int port = 80;
			if (auto colon = authority.find(':'); colon != std::string::npos)
			{
				host = authority.substr(0, colon);
				port = std::atoi(authority.c_str() + colon + 1);
			}

			httplib::Client client(host, port);
			client.set_keep_alive(false);
			auto res = client.Get(path.c_str());
			if (!res)
				return Result<StreamHandle, thx::io::Error>::err(
					{thx::io::ErrorCode::IoError, "http: GET failed for '" + url + "'"});
			if (res->status != 200)
				return Result<StreamHandle, thx::io::Error>::err(
					{thx::io::ErrorCode::IoError, "http: GET '" + url + "' returned status " + std::to_string(res->status)});

			thx::log::info("http: fetched '" + url + "' (" + std::to_string(res->body.size()) + " bytes)");
			return Result<StreamHandle, thx::io::Error>::ok(StreamHandle(new HttpStream(std::move(res->body))));
		}
	};

	class HttpPlugin : public thx::plugin::IPlugin
	{
	public:
		thx::StringView name() const override { return "thx.http.HttpProtocol"; }
		thx::Version version() const override { return thx::Version{1, 0, 0}; }

		thx::Span<const thx::plugin::ServiceRequirement> required() const override
		{
			static const thx::plugin::ServiceRequirement kReqs[] = {
				{thx::io::IIoService::staticId(), thx::io::IIoService::staticVersion()}};
			return {kReqs, 1};
		}

		bool onLoad() override
		{
			m_protocol = std::make_shared<HttpProtocol>();
			return thx::io::addHandler(m_protocol);
		}

		void onUnload() override
		{
			if (m_protocol)
				thx::io::removeHandler(m_protocol.get());
			m_protocol.reset();
		}

	private:
		std::shared_ptr<HttpProtocol> m_protocol;
	};

} // namespace

THX_DEFINE_PLUGIN(HttpPlugin)
