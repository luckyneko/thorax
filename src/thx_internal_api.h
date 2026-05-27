/*
 *  Created by LuckyNeko on 27/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

// THX_INTERNAL_API is a SEPARATE macro from THX_API even though the two
// expand the same way today. They mean different things:
//
//   THX_API           — part of the stable wire ABI. Plugins and external
//                       consumers rely on it. Lives in include/thx/thx_api.h
//                       and is always default-visibility on POSIX (or
//                       dllexport/dllimport on Windows).
//
//   THX_INTERNAL_API  — exposed so the in-tree test binary can link against
//                       internal classes (Registry, ServiceManager,
//                       PluginManager, Library, PluginHandle, PluginGarbage,
//                       ActiveServiceManagerScope). NOT part of the stable
//                       ABI; subject to change. Only emits a visibility
//                       attribute when THX_TESTING is defined. In production
//                       builds (THORAX_BUILD_TESTING=OFF) it expands to
//                       nothing, letting the library's hidden-visibility
//                       default hide the internals from the .so's export
//                       table entirely.
//
// Splitting the two makes intent visible at every call site, and means the
// production-build export surface only contains the API thorax actually
// promises to keep stable.
//
// CMake wires `THX_TESTING` onto both the libthorax target and the
// test-thorax target when `THORAX_BUILD_TESTING` is ON. Both targets see the
// macro consistently — no ODR drift between the library's class definitions
// and the test binary's.

#if defined(THX_TESTING)
#  if defined(_WIN32)
#    if defined(THX_BUILDING)
#      define THX_INTERNAL_API __declspec(dllexport)
#    else
#      define THX_INTERNAL_API __declspec(dllimport)
#    endif
#  elif defined(__GNUC__) || defined(__clang__)
#    define THX_INTERNAL_API __attribute__((visibility("default")))
#  else
#    define THX_INTERNAL_API
#  endif
#else
#  define THX_INTERNAL_API
#endif
