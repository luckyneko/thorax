# io subsystem: a shared INTERFACE library (the headers) plus the IIoService
# provider (a pure scheme dispatcher) and the file:// / http:// handler plugins
# that contribute to it. io is NOT part of libthorax core — it is built on the
# public API like any other consumer (see CLAUDE.md "Scope & inclusion
# criteria"). interface/ is added first so the provider and handlers can depend
# on the io_interface target.
add_subdirectory(interface)
add_subdirectory(service)
add_subdirectory(file)
add_subdirectory(http)
