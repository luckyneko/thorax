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

	thx::service::ServiceManager sm;
	auto services = sm.listServices();
	std::printf("Services registered: %zu\n", services.size());
	return 0;
}
