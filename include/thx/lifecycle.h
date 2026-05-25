/*
 *  Created by LuckyNeko on 25/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include <string>

// Process-wide lifecycle hooks for the framework.
//
// The framework owns a single internal Registry (process-wide singleton). It
// constructs lazily on first use, so calling `initialise()` is not required to
// use the framework — these functions exist for the diagnostic name and for
// the explicit-drain semantics of `shutdown()`.

namespace thx
{
	// Records an optional human-readable name on the framework's internal state.
	// Returns true if this call set the name, false if a previous initialise()
	// already did.
	bool initialise(std::string debugName = "thorax");

	// Drains the deferred-close queue (equivalent to thx::plugin::collectGarbage())
	// and clears the debug name. Does NOT destroy the framework's internal state;
	// safe to call multiple times.
	//
	// Safety: callers MUST release any shared_ptr<IService> references into
	// unloaded DSOs before invoking this.
	void shutdown() noexcept;

} // namespace thx
