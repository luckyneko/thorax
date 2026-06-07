#include "thx/thorax.h"

#include "registry.h"

namespace thx
{
	static std::unique_ptr<Registry> g_registry;
	Registry* registry() noexcept
	{
		return g_registry.get();
	}

	bool initialise(Settings settings)
	{
		if (g_registry)
			return false;

		g_registry = std::make_unique<Registry>(std::move(settings));
		return true;
	}

	void shutdown() noexcept
	{
		if (!g_registry)
			return;
		// Two-step teardown so nothing leaks and nothing crashes:
		//   1. clear() empties the managers *in place* while g_registry is still
		//      valid — a plugin's onUnload reaches back through the facade to
		//      registry(), which must resolve for the whole teardown.
		//   2. reset() then destroys the now-empty shell. ~Registry re-runs
		//      clear(), but with no plugins left it triggers no onUnload and so
		//      never touches the (now-null) global.
		// After this call registry() == nullptr again: zero framework state
		// survives, and the next initialise() builds a fresh singleton.
		g_registry->clear();
		g_registry.reset();
	}
} // namespace thx