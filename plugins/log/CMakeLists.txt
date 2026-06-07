# log subsystem: a header-only INTERFACE library (thx::log::ILogService + the
# redefined LogLevel/LogRecord types + the emit facade) plus the
# plugin_log_service bridge (core LogSink -> registered ILogService) and the
# spdlog backend that implements ILogService. The log subsystem is NOT part of
# libthorax core — core logs through its own thx::LogSink (<thx/log.h>); this
# subsystem is a separate project built on the public plugin/service API like
# any other consumer (see CLAUDE.md "Errors & logging"). interface/ is added
# first so the bridge and the backend can depend on the log_interface target.
add_subdirectory(interface)
add_subdirectory(service)
add_subdirectory(spdlog)
