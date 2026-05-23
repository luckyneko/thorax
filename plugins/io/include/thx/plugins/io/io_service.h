/*
 *  Created by LuckyNeko on 25/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include <thx/service/iservice.h>

#include <memory>

namespace thx::plugins::io
{

// Provider interface.  Implement this to contribute a file format reader.
// IIOService holds only a std::weak_ptr<IFileReader>; when the owning plugin
// releases its shared_ptr the reader is automatically evicted on the next
// read() or addReader() call.
class IFileReader
{
public:
	virtual ~IFileReader() = default;

	// Return true if this reader can handle the given path.
	// Called in registration order; the first reader that returns true wins.
	virtual bool canRead(const char* path) = 0;

	// Read up to (buffer_size - 1) bytes from path into buffer.
	// Returns bytes read on success, -1 on failure.
	virtual int read(const char* path, char* buffer, int buffer_size) = 0;
};

// Main IO service.
//
// Readers are probed in registration order; the first whose canRead() returns
// true handles the request.  Register specific readers before generic ones so
// that the more specific reader wins.
//
// Usage:
//   auto io  = sm.getService<IIOService>();
//   auto txt = io->makeTextReader();
//   io->addReader(txt);            // generic fallback
//   int n = io->read("file.txt", buf, sizeof(buf));
class IIOService : public thx::service::Service<IIOService>
{
public:
	static constexpr thx::Version staticVersion()
	{
		return thx::Version{1, 0, 0};
	}

	// Read a file using the first registered reader whose canRead() is true.
	// Returns bytes read (â‰¥ 0), -1 if no reader accepted the path, or -2 if a
	// reader accepted but the underlying read failed.
	virtual int read(const char* path, char* buffer, int buffer_size) = 0;

	// Register a reader; the service stores a weak_ptr.
	// Reader is probed in the order it was added.
	virtual void addReader(std::shared_ptr<IFileReader> reader) = 0;

	// Eagerly remove a reader by raw-pointer identity.
	virtual void removeReader(IFileReader* reader) = 0;

	// Built-in reader factory: accepts any file, reads raw bytes.
	// Register this last so that more-specific readers get priority.
	virtual std::shared_ptr<IFileReader> makeTextReader() = 0;
};

} // namespace thx::plugins::io
