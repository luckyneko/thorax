# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

> **Maintenance note.** This document is load-bearing for future contributors and for Claude itself. Whenever you change one of the contracts described below — the plugin ABI exports, the registry ownership/lock policy, the unload/DSO-lifetime rules, the logging surface, the export macros, or the layout/install structure — update the affected section here in the same change. If `ROADMAP.md` says something has shipped, this file must already reflect it. Cross-check `ROADMAP.md` for what has shipped before treating any claim here as authoritative.

## Project

Thorax is a C++17 cross-platform plugin framework. The core is a static library (`libthorax.a` / `thorax.lib`) plus optional in-tree plugins. Plugins are shared libraries (`.dylib`/`.so`/`.dll`) loaded at runtime through `thx::PluginLoader` and registered with the `thx::ServiceManager` singleton.

`ROADMAP.md` describes the design intent, the shipped milestones, and the ABI/memory-safety contracts. Read it before making non-trivial changes — many decisions in the codebase are anchored there (e.g. why allocate/destroy round-trip through `thx_create_plugin`/`thx_destroy_plugin`, why `StringView`/`Span` exist instead of `std::string_view`/`std::span` on virtual signatures, why `unload` defers `dlclose` into a graveyard instead of unmapping synchronously).

## Build & test

In-source builds are forbidden by `CMakeLists.txt` — always build into a separate `build/` dir.

```bash
# Configure (Release is the default)
cmake -S . -B build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DTHORAX_SANITIZE=ON   # ASan + UBSan

# Build everything
cmake --build build --parallel

# Run all tests
ctest --test-dir build --output-on-failure

# Run a single test case (Catch2 tag/name match)
build/test-thorax "ServiceManager registers and retrieves a service"
build/test-thorax "[plugin_loader]"          # by tag
build/test-thorax --list-tests
```

CMake options (all default ON when configured as the top-level project, OFF when bundled as a subproject): `THORAX_BUILD_TESTING`, `THORAX_BUILD_EXAMPLES`, `THORAX_BUILD_PLUGINS`, `THORAX_INSTALL`, `THORAX_SANITIZE`.

The test binary is `build/test-thorax`. CTest also runs an `examples.host` integration test and, when `THORAX_INSTALL` is on, an `install.*` smoke test that installs the library into `build/test_install_prefix/` and builds [test/consumer/](test/consumer/) against it via `find_package(Thorax)`.

CI matrix lives in [.github/workflows/](.github/workflows/): macOS (Apple Clang), Linux (GCC 9/12/14, Clang 14/17/18 — Clang 17 runs Debug + ASan/UBSan), Windows (MSVC 2022/2025). GCC 9.1 is the documented minimum (CMake enforces this). Treat warnings as errors on every compiler (`-Werror -Wall -Wextra` / `/WX /W4`), centralised in [cmake/thx_warnings.cmake](cmake/thx_warnings.cmake) via `thx_set_warnings(<target>)`.

## Architecture

The framework is built around four intertwined concepts: **stable identity**, **plugin / service separation**, **safe DSO lifetimes** (including service references that outlive `unload`), and **structured diagnostics**.

### Service identity & lookup

`thx::ServiceID` ([include/thx/service_id.h](include/thx/service_id.h)) is a `constexpr` value object holding an FNV-1a hash plus the original string literal. Equality compares both, so collisions can't produce false matches. The CRTP base [thx::Service<Derived>](include/thx/service.h) auto-derives an ID from the qualified type name (`thx::io::FileService` → `"thx.io.FileService"`) using `detail::TypeName`, so headers shared between plugin and host yield the same ID without any registry lookup. `ServiceID::from<T>()` is the path most code should use; passing a raw string is a fallback.

A service interface is just `struct IFooService : thx::Service<IFooService> { static constexpr thx::Version static_version() {…}; /* virtuals */ };` — the host and the plugin both `#include` that one header. The concrete implementation lives in the plugin's `.cpp` and is wired up via one of the export macros described below.

### Plugin vs. service

`thx::IPlugin` ([include/thx/iplugin.h](include/thx/iplugin.h)) and `thx::IService` ([include/thx/iservice.h](include/thx/iservice.h)) are distinct concepts:

- **`IPlugin`** is what a DSO produces. Each DSO instantiates exactly one `IPlugin`, which reports a name and version, may declare versioned `required()` service dependencies, and registers any number of services in `onLoad(ServiceManager&)` / unregisters them in `onUnload(ServiceManager&)`. The loader rejects the load if `onLoad` returns false or if any `required()` entry is missing or registered at a too-old version.
- **`IService`** is what gets registered in `ServiceManager`. Services have their own `id()`, `version()`, and optional `onConstruct()` / `onDestroy()` lifecycle hooks; they have no concept of which DSO they came from.

The common one-service-per-DSO case is handled by `thx::ServicePluginShim<T>` (same header), which the `THX_DEFINE_SERVICE_PLUGIN(ServiceType)` macro emits for you.

### ServiceManager

[thx::ServiceManager](include/thx/service_manager.h) is a process-wide singleton (`ServiceManager::instance()`) — but the class is also default-constructible, and tests routinely use a local instance. Reads use `std::shared_lock` so concurrent `get_service<T>()` calls never block each other; `register_service`/`unregister_service` take exclusive locks.

**Single-owner semantics:** each `ServiceID` may be registered exactly once. A duplicate `register_service` returns `false` with a `Warn` diagnostic and *does not* invoke the supplied factory. Plugins that want to *contribute* to an existing service (rather than replace it) use the provider pattern exposed by that service — see the logging/io services for the canonical shape (`add_backend` / `add_reader`, holding `weak_ptr` to providers).

**Lifecycle hooks:**
- `IService::onConstruct()` runs *without* the registry lock (Milestone 11). The registry uses a phase-1 reservation pattern so concurrent registrations of the same ID still serialize cleanly, but the factory and `onConstruct` callback may call back into `ServiceManager` without deadlocking.
- `IService::onDestroy()` runs (also without the lock) inside `unregister_service`, after the entry has been removed from the map but *while a `shared_ptr` to the service is still alive*. The service object itself is destroyed when the last `shared_ptr` to it goes out of scope, which may be later than `onDestroy()` if any caller is still holding a handle.

### Plugin ABI & memory safety

The ABI contract is "allocate and free on the same side of the DSO boundary." Every plugin shared library exports exactly three C symbols, decorated via the single `THX_PLUGIN_API` macro ([include/thx/platform.h](include/thx/platform.h)) — `extern "C"` plus the platform DLL-export attribute, plus default visibility so a plugin built with `-fvisibility=hidden` still exports them:

```
THX_PLUGIN_API thx::IPlugin* thx_create_plugin();
THX_PLUGIN_API void          thx_destroy_plugin(thx::IPlugin*);
THX_PLUGIN_API uint32_t      thx_abi_version();    // pack_version(THORAX_VERSION) at plugin compile-time
```

Plugin authors don't write these by hand; they use one of:

- `THX_DEFINE_SERVICE_PLUGIN(ServiceType)` — common case. Emits an `IPlugin` shim that registers exactly one service of the given type in `onLoad` and unregisters it in `onUnload`. `ServiceType` must inherit from `thx::Service<ServiceType>`, define `static_version()`, and be default-constructible.
- `THX_DEFINE_PLUGIN(PluginType)` — power-user form. The author supplies their own `IPlugin` subclass, free to register multiple services, declare `required()` dependencies, or hold per-DSO state.

Both macros emit `thx_abi_version()` returning `pack_version(THORAX_VERSION)`. `PluginHandle::open()` unpacks this and applies `compatible(plugin_version, host_version)`: same major and host's full major.minor.patch ≥ plugin's. A plugin built against a newer thorax than the host is rejected; a plugin built against the same major but older minor/patch is accepted.

Anything that crosses a virtual boundary on an `IService` API must use ABI-stable types — primitives, C strings, `thx::StringView`, `thx::Span<T>`, `thx::Version`. Do not put `std::string_view`, `std::span`, `std::string`, `std::vector`, or other STL containers in virtual signatures plugins implement; their layout is not stable across compilers/CRTs.

### Plugin loader & DSO lifetimes

[thx::PluginHandle](include/thx/plugin_handle.h) is the RAII DSO wrapper (`dlopen`/`dlclose` on POSIX, `LoadLibraryEx`/`FreeLibrary` on Windows). It resolves the three exports on `open()` and rejects an incompatible `thx_abi_version()` before any service is registered.

[thx::PluginLoader](include/thx/plugin_loader.h) sits on top:
- `load(path)` opens the DSO, instantiates the `IPlugin`, checks `required()`, calls `onLoad`, and attributes any newly-registered service IDs to that plugin.
- `unload(path)` calls `IPlugin::onUnload` and removes the loader's entry. **It does not call `dlclose` directly** — instead the native handle goes onto a process-wide deferred-close graveyard.
- `discover(dir)` returns the sorted list of files matching `kPluginExtension`; `discover_and_load(dir)` calls `load` on each and returns a `LoadSummary { loaded, failed }` rather than a single `Result`, so callers can decide what counts as success.

The loader keys entries by canonical path so loading the same file twice is a no-op. It is **not** thread-safe; serialise externally if needed. The graveyard itself is thread-safe.

**DSO keep-alive (Milestone 8b).** The deferred-close graveyard is what makes it safe for callers to hold `shared_ptr<IService>` handles across `unload()`: the service's destructor and its `shared_ptr` control block both live in plugin code, so the DSO must stay mapped until every reference into it has been released. The graveyard drains on two occasions:

1. Automatically at the start of `PluginLoader::load()`, so long-running programs don't accumulate mapped-but-unused DSOs;
2. On demand via `thx::collect_plugin_garbage()` (returns the number of DSOs actually unmapped). `thx::pending_plugin_garbage()` exposes the current queue depth.

**Hard rule:** every `shared_ptr<IService>` into a DSO must be released before the next drain. Because `load()` drains first, this means: if you have unloaded a plugin and are still holding service references, do **not** call `load()` until those references have been dropped. Tests that exercise this contract live in [test/test_plugin_loader.cpp](test/test_plugin_loader.cpp) under the `[lifetime]` tag.

`PluginLoader::~PluginLoader` calls `onUnload` for every still-loaded plugin and clears its entries, but does **not** drain the graveyard. Drain explicitly when no service references into those DSOs remain.

### Errors & logging

Failures return `thx::Result<T, thx::Error>` ([include/thx/result.h](include/thx/result.h)) — no exceptions in library code. `thx::Result<void, Error>` is the void specialisation. `Result<T>` exposes `value_or(fallback)` and `map(f)`; `discover_and_load` is the one operation that breaks the pattern (it returns a `LoadSummary` so callers can react to partial failure).

Diagnostics flow through a pluggable `thx::ILogSink` ([include/thx/log.h](include/thx/log.h)). The default sink writes structured lines to `stderr`. Replace per-process with `thx::set_log_sink(sink)`; passing `nullptr` silences logging entirely. `thx::restore_default_log_sink()` brings the built-in stderr sink back. `ServiceManager` and `PluginLoader` emit structured records with source locations for every state change and failure.

There are no `THX_LOG` / `THX_ASSERT` macros. Source location is captured automatically via `__builtin_FILE`/`__builtin_LINE`/`__builtin_FUNCTION` defaults on GCC, Clang, and MSVC ≥ VS 2019 16.6 (`_MSC_VER 1926`). Call sites use the free functions directly:

```cpp
thx::log(thx::LogLevel::Warn, "message");
thx::assert_that(condition, "message");   // logs at Error if false; std::abort() in Debug builds only
```

`assert_that` never silently swallows its condition — it always emits the diagnostic before deciding whether to abort.

### Versioning

[thx::Version](include/thx/version_type.h) is full semver 2.0 with `constexpr` comparison. `thx::THORAX_VERSION` is generated from the CMake project version into [include/thx/version.h.in](include/thx/version.h.in). `pack_version(Version)` packs major/minor/patch into a `uint32_t` (8/8/16 bits) for crossing the C plugin ABI; `unpack_version` is the inverse (pre-release and build metadata are not represented in the packed form — the loader gate uses only the three numeric components). `compatible(required, provided)` is the single function used both by `PluginHandle::open()` to gate `thx_abi_version()` and by `PluginLoader` to check each `ServiceRequirement` reported by `IPlugin::required()`.

## Layout & conventions

```
include/thx/         public API headers (one concern per header; thorax.h is the umbrella include)
include/thx/detail/  implementation helpers (hash, semver parser, type_name, format) — installed alongside the public headers because they're transitively included, but not part of the user-facing surface
src/                 .cpp for the headers above
plugins/             in-tree plugins (logging, io). Each is a SHARED lib using THX_DEFINE_SERVICE_PLUGIN (or THX_DEFINE_PLUGIN for the multi-service form)
plugins/<name>/include/thx/plugins/<name>/<name>_service.h  the shared interface header
examples/            example host + two example plugins; integration test runs example_host
test/                Catch2 unit + integration tests. mock_plugin{,_bad_abi,_multi,_bails,_requires_newer}/ are built as SHARED for the loader/IPlugin tests
test/consumer/       standalone CMake project used by the install smoke test
cmake/               ThoraxConfig.cmake.in, addcatch2.cmake fetcher, and thx_warnings.cmake (thx_set_warnings(<target>))
thirdparty/          vendored Catch2 tarball (downloaded on demand by addcatch2.cmake)
```

Style is enforced by [.clang-format](.clang-format): Allman braces, **tabs for indent (width 4)**, no column limit, namespace contents indented, pointer-left (`int* p`), access modifiers offset −4. Match the existing files when editing.

**Adding a test file** means appending it to the `add_executable(test-${PROJECT_NAME} …)` source list in [CMakeLists.txt](CMakeLists.txt). Tests for in-tree plugins are guarded by `if(THORAX_BUILD_PLUGINS)` and pass plugin paths via `THX_*_PLUGIN_PATH` compile definitions.

**Adding a new mock plugin** for loader tests means: a new directory under `test/`, an `add_library(... SHARED ...)` block in the top-level CMakeLists that links `${PROJECT_NAME}`, calls `thx_set_warnings`, and applies `-fvisibility=hidden` on non-MSVC; a new `THX_*_PLUGIN_PATH` compile definition on `test-thorax`; and a corresponding `add_dependencies(test-${PROJECT_NAME} …)` entry. Look at `mock_plugin_multi` or `mock_plugin_requires_newer` for the current template.

**Adding a new in-tree plugin:** create `plugins/<name>/{CMakeLists.txt,src,include/thx/plugins/<name>}`, add `add_subdirectory(<name>)` to [plugins/CMakeLists.txt](plugins/CMakeLists.txt), call `thx_set_warnings(plugin_<name>)` and add `-fvisibility=hidden` on non-MSVC inside its CMakeLists, then add `install(TARGETS plugin_<name> …)` plus the `install(DIRECTORY plugins/<name>/include/ …)` block in the `THORAX_INSTALL` section of the root CMakeLists. The plugin's interface header must live under `plugins/<name>/include/thx/plugins/<name>/` so consumers include it as `<thx/plugins/<name>/<name>_service.h>` after install.

## Work tracking

Concrete follow-up items — known issues, hardening tasks, doc gaps — are tracked in [WORK.md](WORK.md). Add to it as new issues are spotted; clear items as they ship.
