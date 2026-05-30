/*
 *  Created by LuckyNeko on 25/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

// THX_API decorates symbols that cross the libthorax shared-library boundary.
//
// - On Windows it expands to __declspec(dllexport) when building the library
//   (THX_BUILDING defined) and __declspec(dllimport) when consuming.
// - On GCC/Clang it expands to __attribute__((visibility("default"))) so the
//   symbol survives the library's -fvisibility=hidden default.
//
// What gets marked:
//
// - Every public free function in thx::, thx::service::, thx::plugin:: that
//   has its definition inside the library (not header-inline).
// - Abstract interface classes whose typeinfo must agree across the boundary
//   for dynamic_pointer_cast: IService, IPlugin, ILogSink. Marked at class
//   scope.
// - Internal classes reached from the in-tree test binary (Registry,
//   ServiceManager, PluginManager, PluginHandle, PluginGarbage, Library).
//   Their headers live in src/ and are never installed, so external consumers
//   cannot accidentally reach them; the THX_API marks exist purely so the
//   test binary can link.
//
// Templates and value types do NOT need THX_API — they instantiate in the
// consumer's translation unit.

#if defined(_WIN32)
#	if defined(THX_BUILDING)
#		define THX_API __declspec(dllexport)
#	else
#		define THX_API __declspec(dllimport)
#	endif
#elif defined(__GNUC__) || defined(__clang__)
#	define THX_API __attribute__((visibility("default")))
#else
#	define THX_API
#endif
