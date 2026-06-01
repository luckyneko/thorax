/*
 *  Created by LuckyNeko on 01/06/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "thx/io/io.h"

#include "io/io_service.h"
#include "registry.h"

namespace thx::io
{

	Result<StreamHandle, Error> open(StringView address, Mode mode)
	{
		return thx::Registry::instance().ioService().open(address, mode);
	}

	bool addHandler(std::shared_ptr<IProtocol> handler)
	{
		if (!handler)
			return false;
		thx::Registry::instance().ioService().addHandler(std::move(handler));
		return true;
	}

	void removeHandler(IProtocol* handler)
	{
		thx::Registry::instance().ioService().removeHandler(handler);
	}

} // namespace thx::io
