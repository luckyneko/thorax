# The io subsystem's interface — IIoService / IProtocol / IStream, the error
# domain, and the facade. A header-only INTERFACE library: the provider plugin
# IMPLEMENTS it and the handler plugins CONSUME it, so all of them share one
# declared dependency for the headers (and the transitive thorax link) without
# linking any io binary. Handlers resolve the live IIoService at runtime via
# getService<IIoService>(), never by linking the provider DSO.
#
# This is the io ABI contract: IIoService's vtable is append-only, and breaking
# changes bump IIoService::staticVersion() — handlers declare a matching
# `required()`, so the loader's Version::compatible check rejects a drift at
# load rather than mis-dispatching. (See CLAUDE.md "Streaming I/O".)
add_library(io_interface INTERFACE)
target_include_directories(io_interface INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
# Pulls in the core thorax headers (result.h, service/service.h, …) the io
# headers include, plus the libthorax link every io plugin needs anyway.
target_link_libraries(io_interface INTERFACE ${PROJECT_NAME})
