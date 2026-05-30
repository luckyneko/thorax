/*
 *  Minimal consumer that verifies find_package(Thorax) wires up headers and
 *  the static library correctly.  Built and run as part of the install smoke-test.
 */

#include <thx/thorax.h>

#include <cstdio>

int main()
{
	std::printf("Thorax %u.%u.%u\n",
				thx::THORAX_VERSION.major,
				thx::THORAX_VERSION.minor,
				thx::THORAX_VERSION.patch);

	// ServiceManager is now an implementation detail. Consumers use the
	// free-function facade (forwards to the framework's internal manager).
	auto services = thx::service::listServices();
	std::printf("Services registered: %zu\n", services.size());
	return 0;
}
