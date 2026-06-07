# The log subsystem's interface — thx::log::ILogService plus the redefined
# LogLevel / LogRecord value types (<thx/log/log_record.h>) and the emit facade
# (<thx/log/log.h>), the service-based logging API. A header-only INTERFACE
# library: the spdlog backend IMPLEMENTS ILogService and the plugin_log_service
# bridge CONSUMES it (resolving the live service at runtime via
# getService<ILogService>()), so both share one declared dependency for the
# headers (and the transitive thorax link) without linking any log binary.
#
# The log subsystem is a separate project on thorax's plugin/service machinery,
# NOT core: libthorax core logs through its own thx::LogSink (<thx/log.h>) and
# the subsystem deliberately does not depend on it — it redefines LogLevel /
# LogRecord in thx::log, the way io carries its own thx::io::Error. This mirrors
# io_interface: the vtable is append-only, and a breaking change bumps
# ILogService::staticVersion(), which the bridge's required() is checked against
# at load. (See CLAUDE.md "Errors & logging".)
add_library(log_interface INTERFACE)
target_include_directories(log_interface INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
# Pulls in the core thorax ABI primitives the interface headers reuse
# (string_view.h, rtti/source_location.h, service/iservice.h, version_type.h),
# plus the libthorax link every log plugin needs anyway. It does NOT pull in
# core's <thx/log.h> — only the bridge does, to translate at the boundary.
target_link_libraries(log_interface INTERFACE ${PROJECT_NAME})
