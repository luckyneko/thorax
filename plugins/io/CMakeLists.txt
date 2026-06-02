# io subsystem: the IIoService provider (a pure scheme dispatcher) plus the
# file:// and http:// handler plugins that contribute to it. io is NOT part of
# libthorax core — it is built on the public API like any other consumer (see
# CLAUDE.md "Scope & inclusion criteria"). The provider owns the io interface
# headers under service/include/thx/io; the handlers share them.
set(THX_IO_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/service/include)
add_subdirectory(service)
add_subdirectory(file)
add_subdirectory(http)
