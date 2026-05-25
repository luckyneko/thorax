/*
 *  Created by LuckyNeko on 25/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

// Anchor file: provides out-of-line key functions for the abstract interface
// classes that cross the libthorax shared-library boundary.
//
// The Itanium C++ ABI emits a class's vtable and typeinfo in whichever TU
// defines the class's first non-inline virtual function ("key function"). If
// every virtual is inline (as is the case for our pure-interface classes),
// the vtable is emitted as a weak COMDAT in every TU that uses it — and on
// platforms without cross-DSO weak-symbol coalescing (notably macOS two-level
// namespace), each shared object ends up with its own typeinfo at a distinct
// address. dynamic_pointer_cast<T>(shared_ptr<IService>) then fails across
// the plugin boundary even when libthorax is SHARED.
//
// Defining the virtual destructors out-of-line here forces libthorax.dylib /
// libthorax.so / thorax.dll to own the canonical typeinfo for each interface,
// and the dynamic linker's two-level lookup resolves every external reference
// back to the library's symbol.

#include "thx/log.h"
#include "thx/plugin/iplugin.h"
#include "thx/service/iservice.h"

namespace thx
{
	ILogSink::~ILogSink() = default;
}

namespace thx::service
{
	IService::~IService() = default;
}

namespace thx::plugin
{
	IPlugin::~IPlugin() = default;
}
