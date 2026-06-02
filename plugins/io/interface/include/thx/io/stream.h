/*
 *  Created by LuckyNeko on 01/06/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "thx/io/mode.h"
#include "thx/span.h"

#include <cstddef>
#include <cstdint>

namespace thx::io
{
	// An opened byte stream. Created by an IProtocol and handed back to the
	// caller wrapped in a StreamHandle. Only ABI-stable types cross the vtable
	// (thx::Span, primitives), so a plugin may implement it.
	//
	// LIFETIME / DSO caveat: a stream's vtable + destructor live in whatever DSO
	// created it. Every stream comes from a handler plugin (file, http, …), so
	// the owning StreamHandle must be dropped before that plugin is unloaded —
	// the same rule as holding a ServiceHandle across unload.
	//
	// As a pure interface with an inline virtual destructor it needs no abi.cpp
	// anchor: StreamHandle owns it by IStream* and never dynamic_casts.
	class IStream
	{
	public:
		virtual ~IStream() = default;

		// Read up to buffer.size() bytes. Returns bytes read, 0 at end of
		// stream, or a negative value on error.
		virtual std::int64_t read(Span<std::uint8_t> buffer) = 0;

		// Write data.size() bytes. Returns bytes written, or negative on error.
		virtual std::int64_t write(Span<const std::uint8_t> data) = 0;

		// Reposition (when canSeek()). Returns the new absolute offset, or
		// negative if unsupported/failed. Default: unsupported.
		virtual std::int64_t seek(std::int64_t offset, Whence whence)
		{
			(void)offset;
			(void)whence;
			return -1;
		}

		// Current absolute offset, or negative if unsupported.
		virtual std::int64_t tell() const { return -1; }

		virtual bool canRead() const = 0;
		virtual bool canWrite() const = 0;
		virtual bool canSeek() const { return false; }
	};

	// Move-only owning handle over an IStream (unique ownership — streams are
	// not shared). Single pointer, sizeof == sizeof(void*) (asserted below).
	// ~StreamHandle runs `delete` through IStream's virtual destructor, so the
	// concrete operator delete executes in the DSO that created the stream —
	// the same mechanism that keeps ServiceHandle's release allocator-safe
	// across the boundary.
	class StreamHandle
	{
		IStream* m_ptr = nullptr;

	public:
		constexpr StreamHandle() noexcept = default;
		constexpr StreamHandle(std::nullptr_t) noexcept {}

		explicit StreamHandle(IStream* p) noexcept
			: m_ptr(p)
		{
		}

		StreamHandle(StreamHandle&& other) noexcept
			: m_ptr(other.m_ptr)
		{
			other.m_ptr = nullptr;
		}

		StreamHandle& operator=(StreamHandle&& other) noexcept
		{
			if (this != &other)
			{
				delete m_ptr;
				m_ptr = other.m_ptr;
				other.m_ptr = nullptr;
			}
			return *this;
		}

		StreamHandle(StreamHandle const&) = delete;
		StreamHandle& operator=(StreamHandle const&) = delete;

		~StreamHandle() { delete m_ptr; }

		IStream* get() const noexcept { return m_ptr; }
		IStream* operator->() const noexcept { return m_ptr; }
		IStream& operator*() const noexcept { return *m_ptr; }
		explicit operator bool() const noexcept { return m_ptr != nullptr; }

		void reset() noexcept
		{
			delete m_ptr;
			m_ptr = nullptr;
		}

		// Relinquish ownership without destroying the stream.
		IStream* release() noexcept
		{
			IStream* p = m_ptr;
			m_ptr = nullptr;
			return p;
		}
	};

	static_assert(sizeof(StreamHandle) == sizeof(void*),
				  "StreamHandle must be a single-pointer type for ABI stability");

} // namespace thx::io
