/*
 *  Created by LuckyNeko on 01/06/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

namespace thx::io
{
	// How an address is opened. Not every protocol supports every mode (an
	// http:// stream is read-only, for example); open() fails for an
	// unsupported mode.
	enum class Mode
	{
		Read,
		Write,
		ReadWrite,
		Append,
	};

	// Reference point for IStream::seek().
	enum class Whence
	{
		Begin,
		Current,
		End,
	};

} // namespace thx::io
