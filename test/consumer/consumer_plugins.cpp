/*
 *  Smoke-test that plugin headers are correctly installed and usable.
 *  Compiled only when THX_LOGGING_INCLUDE and THX_IO_INCLUDE are found.
 */

#include <thx/plugins/logging/logging_service.h>
#include <thx/plugins/io/io_service.h>

#include <cstdio>

int main()
{
    // Verify the service IDs are reachable as constexpr values.
    constexpr auto logging_id = thx::plugins::logging::ILoggingService::staticId();
    constexpr auto io_id      = thx::plugins::io::IIOService::staticId();

    std::printf("logging service id: %s\n", logging_id.name());
    std::printf("io service id:      %s\n", io_id.name());
    return 0;
}
