# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

> **Maintenance note.** This document is load-bearing for future contributors and for Claude itself. Whenever you change one of the contracts described below — the plugin ABI exports, the registry ownership/lock policy, the unload/DSO-lifetime rules, the logging surface, the export macros, or the layout/install structure — update the affected section here in the same change. If `ROADMAP.md` says something has shipped, this file must already reflect it. Cross-check `ROADMAP.md` for what has shipped before treating any claim here as authoritative.

## Project

Thorax is a C++17 cross-platform plugin framework. The core is a static library (`libthorax.a` / `thorax.lib`) plus optional in-tree plugins. Plugins are shared libraries (`.dylib`/`.so`/`.dll`) loaded at runtime through `thx::plugin::PluginManager` and registered with the `thx::service::ServiceManager` singleton.

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
build/test-thorax "[plugin_manager]"          # by tag
build/test-thorax --list-tests
```

CMake options (all default ON when configured as the top-level project, OFF when bundled as a subproject): `THORAX_BUILD_TESTING`, `THORAX_BUILD_EXAMPLES`, `THORAX_BUILD_PLUGINS`, `THORAX_INSTALL`, `THORAX_SANITIZE`.

The test binary is `build/test-thorax`. CTest also runs an `examples.host` integration test and, when `THORAX_INSTALL` is on, an `install.*` smoke test that installs the library into `build/test_install_prefix/` and builds [test/consumer/](test/consumer/) against it via `find_package(Thorax)`.

CI matrix lives in [.github/workflows/](.github/workflows/): macOS (Apple Clang), Linux (GCC 9/12/14, Clang 14/17/18 — Clang 17 runs Debug + ASan/UBSan), Windows (MSVC 2022/2025). GCC 9.1 is the documented minimum (CMake enforces this). Treat warnings as errors on every compiler (`-Werror -Wall -Wextra` / `/WX /W4`), centralised in [cmake/thx_warnings.cmake](cmake/thx_warnings.cmake) via `thx_set_warnings(<target>)`.

## Architecture

The framework is built around four intertwined concepts: **stable identity**, **plugin / service separation**, **safe DSO lifetimes** (including service references that outlive `unload`), and **structured diagnostics**.

### Service identity & lookup

`thx::service::ServiceID` ([include/thx/service/service_id.h](include/thx/service/service_id.h)) is a `constexpr` value object holding an FNV-1a hash plus the original string literal. Equality compares both, so collisions can't produce false matches. The CRTP base [thx::service::Service<Derived>](include/thx/service/service.h) auto-derives an ID from the qualified type name (`thx::io::FileService` → `"thx.io.FileService"`) using `detail::TypeName`, so headers shared between plugin and host yield the same ID without any registry lookup. `ServiceID::from<T>()` is the path most code should use; passing a raw string is a fallback.

A service interface is just `struct IFooService : thx::service::Service<IFooService> { static constexpr thx::Version staticVersion() {…}; /* virtuals */ };` — the host and the plugin both `#include` that one header. The concrete implementation lives in the plugin's `.cpp` and is wired up via one of the export macros described below.

### Plugin vs. service

`thx::plugin::IPlugin` ([include/thx/plugin/iplugin.h](include/thx/plugin/iplugin.h)) and `thx::service::IService` ([include/thx/service/iservice.h](include/thx/service/iservice.h)) are distinct concepts:

- **`IPlugin`** is what a DSO produces. Each DSO instantiates exactly one `IPlugin`, which reports a name and version, may declare versioned `required()` service dependencies, and registers any number of services in `onLoad(ServiceManager&)` / unregisters them in `onUnload(ServiceManager&)`. The loader rejects the load if `onLoad` returns false or if any `required()` entry is missing or registered at a too-old version.
- **`IService`** is what gets registered in `ServiceManager`. Services have their own `id()`, `version()`, and optional `onConstruct()` / `onDestroy()` lifecycle hooks; they have no concept of which DSO they came from.

The common one-service-per-DSO case is handled by `thx::plugin::ServicePluginShim<T>` (same header), which the `THX_DEFINE_SERVICE_PLUGIN(ServiceType)` macro emits for you.

### Registry

[thx::Registry](include/thx/registry.h) is the framework's only static singleton. It owns three things by value: `PluginGarbage`, `ServiceManager`, and `PluginManager` — declared in that order so destruction runs `~PluginManager` → `~ServiceManager` → `~PluginGarbage`, which is the only correct order (the manager schedules DSOs into the garbage queue at teardown, so the queue must outlive it). `Registry::instance()` constructs lazily on first call and persists until program exit. Member addresses are stable — callers may take and hold references freely.

Access pattern: `thx::registry().serviceManager()` / `.pluginManager()` / `.pluginGarbage()`. The individual manager classes deliberately do **not** expose their own `::instance()` accessors; Registry is the one entry point. Tests that need isolated state continue to construct local `ServiceManager` / `PluginManager` instances directly (the public default constructors are preserved for this).

Lifecycle hooks (all free functions in the `thx` namespace, declared in `registry.h`):

- `thx::initialise(debugName)` — records an optional human-readable name on the Registry. Returns `true` if this call set the name, `false` if a previous `initialise()` already did. Calling `initialise()` is *not* required to use the framework; it's purely for diagnostics.
- `thx::shutdown()` — drains the deferred-close queue (via `PluginGarbage::collect()`) and clears the debug name. Does **not** destroy the Registry — the singleton persists until program exit. Safe to call multiple times. Callers MUST release any `shared_ptr<IService>` references into unloaded DSOs before invoking it.
- `thx::registry()` — shorthand for `Registry::instance()`.

User-facing free-function shims `thx::collectPluginGarbage()` / `thx::pendingPluginGarbage()` operate on the Registry-owned queue and remain the recommended entry points for code that just wants to drain.

### Free-function facades

Two "system-level" headers — `thx/<layer>/<layer>.h` — reduce the verbosity of `thx::registry().xxxManager().method(...)` for typical host code:

- [thx/service/service.h](include/thx/service/service.h) — inline `thx::service::registerService<T>`, `unregisterService<T>`, `getService<T>`, `listServices`. Same names as the matching `ServiceManager` methods.
- [thx/plugin/plugin.h](include/thx/plugin/plugin.h) — inline `thx::plugin::discover`, `open`, `load`, `discoverAndLoad`, `unload`, `isLoaded`, `listPlugins`, `checkRequirements`. Same names as the matching `PluginManager` methods.

Each facade is a one-liner forwarding to the Registry-owned manager. Code that already holds a `ServiceManager&` or `PluginManager&` (e.g. inside `IPlugin::onLoad`, or tests using a local instance) should keep calling the member functions directly — the facades exist for the everywhere-else case where there's no manager reference in scope. The example host (`examples/host/main.cpp`) uses the facades and is the canonical demonstration.

**`#include` policy.** Service authors writing an interface type `IFooService : thx::service::Service<IFooService>` should `#include "thx/service/iservice.h"` — the CRTP base `Service<>` is paired with `IService` there. Host code calling the facade functions includes `thx/service/service.h` and `thx/plugin/plugin.h`. The umbrella `thx/thorax.h` brings in everything.

### ServiceManager

[thx::service::ServiceManager](include/thx/service/service_manager.h) is owned by the process-wide [thx::Registry](include/thx/registry.h); reach for it via `thx::registry().serviceManager()`. The class is also default-constructible, and tests routinely use a local instance. Reads use `std::shared_lock` so concurrent `getService<T>()` calls never block each other; `registerService`/`unregisterService` take exclusive locks.

**Single-owner semantics:** each `ServiceID` may be registered exactly once. A duplicate `registerService` returns `false` with a `Warn` diagnostic and *does not* invoke the supplied factory. Plugins that want to *contribute* to an existing service (rather than replace it) use the provider pattern exposed by that service — see the logging/io services for the canonical shape (`addBackend` / `addReader`, holding `weak_ptr` to providers).

**Lifecycle hooks:**
- `IService::onConstruct()` runs *without* the registry lock (Milestone 11). The registry uses a phase-1 reservation pattern so concurrent registrations of the same ID still serialize cleanly, but the factory and `onConstruct` callback may call back into `ServiceManager` without deadlocking.
- `IService::onDestroy()` runs (also without the lock) inside `unregisterService`, after the entry has been removed from the map but *while a `shared_ptr` to the service is still alive*. The service object itself is destroyed when the last `shared_ptr` to it goes out of scope, which may be later than `onDestroy()` if any caller is still holding a handle.

### Plugin ABI & memory safety

The ABI contract is "allocate and free on the same side of the DSO boundary." Every plugin shared library exports exactly three C symbols, decorated via the single `THX_PLUGIN_API` macro ([include/thx/plugin/platform.h](include/thx/plugin/platform.h)) — `extern "C"` plus the platform DLL-export attribute, plus default visibility so a plugin built with `-fvisibility=hidden` still exports them:

```
THX_PLUGIN_API thx::plugin::IPlugin* thx_create_plugin();
THX_PLUGIN_API void          thx_destroy_plugin(thx::plugin::IPlugin*);
THX_PLUGIN_API uint32_t      thx_abi_version();    // THORAX_VERSION.pack() at plugin compile-time
```

Plugin authors don't write these by hand; they use one of:

- `THX_DEFINE_SERVICE_PLUGIN(ServiceType)` — common case. Emits an `IPlugin` shim that registers exactly one service of the given type in `onLoad` and unregisters it in `onUnload`. `ServiceType` must inherit from `thx::service::Service<ServiceType>`, define `staticVersion()`, and be default-constructible.
- `THX_DEFINE_PLUGIN(PluginType)` — power-user form. The author supplies their own `IPlugin` subclass, free to register multiple services, declare `required()` dependencies, or hold per-DSO state.

Both macros emit `thx_abi_version()` returning `THORAX_VERSION.pack()`. `PluginHandle::open()` unpacks this via `Version(uint32_t)` and applies `Version::compatible(plugin_version, host_version)`: same major and host's full major.minor.patch ≥ plugin's. A plugin built against a newer thorax than the host is rejected; a plugin built against the same major but older minor/patch is accepted.

Anything that crosses a virtual boundary on an `IService` API must use ABI-stable types — primitives, C strings, `thx::StringView`, `thx::Span<T>`, `thx::Version`. Do not put `std::string_view`, `std::span`, `std::string`, `std::vector`, or other STL containers in virtual signatures plugins implement; their layout is not stable across compilers/CRTs.

### Plugin loader & DSO lifetimes

DSO loading is layered: [thx::Library](include/thx/library.h) is the generic RAII wrapper around `dlopen`/`dlclose` on POSIX and `LoadLibraryEx`/`FreeLibrary` on Windows. It offers a fluent `open(path).bind("symbol", fnPtr).bind(...)` chain — `valid()` / `operator bool()` tells you whether the chain succeeded, `error()` carries the platform diagnostic. [thx::plugin::PluginHandle](include/thx/plugin/plugin_handle.h) sits on top of `Library`, resolving the three `thx_*` exports on `open()` and rejecting an incompatible `thx_abi_version()` before any service is registered. Failure paths in `PluginHandle::open()` close the `Library` synchronously (no plugin code has run yet); successful unloads move the `Library` into `PluginGarbage` for deferred close.

`thx::LIBRARY_EXTENSION` (in `library.h`) is the platform DSO suffix — `.dylib` / `.so` / `.dll` — used by `PluginManager::discover()` and any consumer that scans a directory.

[thx::plugin::PluginManager](include/thx/plugin/plugin_manager.h) sits on top. The full lifecycle is **discover → open → load**:
- `discover(dir)` returns the sorted list of files matching `LIBRARY_EXTENSION`. Pure filesystem scan; nothing is mapped.
- `open(path)` opens the DSO, ABI-checks it, and instantiates its `IPlugin`, returning a move-only `OpenedPlugin` value. **`required()` is NOT yet checked and `onLoad` is NOT yet called.** The caller queries `name()`/`version()`/`required()`/`path()` to plan load order across many plugins, then commits with `load(OpenedPlugin)`. Dropping the value without loading destroys the `IPlugin` and queues the DSO to the graveyard. `open()` is the entry point that drains the graveyard (see "DSO keep-alive" below).
- `load(OpenedPlugin)` checks `required()` against the registry, calls `onLoad`, and takes ownership of the DSO + `IPlugin` on success. The `OpenedPlugin` is consumed either way; on failure its DSO is released to the graveyard at the next drain. `PluginManager::checkRequirements(sm, reqs)` is exposed as a static dry-run helper so callers can pre-check a requirement set without consuming an `OpenedPlugin`.
- `load(path)` is a convenience wrapper that does `open(path)` + `load(OpenedPlugin)` in one step. Unlike `open()` (which returns `AlreadyLoaded` on duplicate paths), `load(path)` preserves the historical "loading the same file twice is a no-op" behavior by checking `isLoaded()` first.
- `unload(path)` calls `IPlugin::onUnload` and removes the manager's entry. **It does not call `dlclose` directly** — instead the native handle goes onto a process-wide deferred-close queue managed by `thx::plugin::PluginGarbage` (see "DSO keep-alive" below).
- `discoverAndLoad(dir)` calls `load(path)` on each discovered file and returns a `LoadSummary { loaded, failed }` rather than a single `Result`, so callers can decide what counts as success. It does not surface `required()` or do any ordering — use the explicit `discover → open* → sort → load*` flow when you need that.

The manager keys entries by canonical path so loading the same file twice via `load(path)` is a no-op (and via `open()` reports `AlreadyLoaded`). It is **not** thread-safe; serialise externally if needed. The garbage queue itself is thread-safe.

**DSO keep-alive (Milestone 8b).** The deferred-close queue lives in [thx::plugin::PluginGarbage](include/thx/plugin/plugin_garbage.h) — owned by the process-wide `Registry`, accessible via `thx::registry().pluginGarbage()`. The class wraps a mutex + `vector<Library>` with `schedule(Library)`, `collect()`, and `pending()` members. `PluginHandle::close()` moves its `Library` into the queue instead of letting `~Library` run `dlclose`/`FreeLibrary` synchronously. This indirection is what makes it safe for callers to hold `shared_ptr<IService>` handles across `unload()`: the service's destructor and its `shared_ptr` control block both live in plugin code, so the DSO must stay mapped until every reference into it has been released. The queue drains on two occasions:

1. Automatically at the start of `PluginManager::open()` (and therefore also the convenience `load(path)` overload, which calls `open()` internally), so long-running programs don't accumulate mapped-but-unused DSOs. `load(OpenedPlugin)` itself does *not* drain — meaning a `discover → open* → load*` batch drains exactly once, at the start of the open phase, and never yanks a DSO while another plugin is still being inspected;
2. On demand via `thx::registry().pluginGarbage().collect()` (or the equivalent free-function shim `thx::collectPluginGarbage()`). `thx::pendingPluginGarbage()` exposes the current queue depth.

The class lives separately from `PluginManager` because the queue has to outlive any individual manager: a caller may destroy the `PluginManager` and still hold a service reference, which the queue keeps the DSO mapped for. `Registry` owns the `PluginGarbage` by value, declared *before* the `ServiceManager` so it is destroyed *after* — anything that schedules at teardown still finds a live queue.

**Hard rule:** every `shared_ptr<IService>` into a DSO must be released before the next drain. Because `open()` drains first, this means: if you have unloaded a plugin and are still holding service references, do **not** call `open()` (or `load(path)`) until those references have been dropped. Tests that exercise this contract live in [test/test_plugin_manager.cpp](test/test_plugin_manager.cpp) under the `[lifetime]` tag.

`PluginManager::~PluginManager` calls `onUnload` for every still-loaded plugin and clears its entries, but does **not** drain `PluginGarbage`. Drain explicitly when no service references into those DSOs remain.

### Errors & logging

Failures return `thx::Result<T, thx::Error>` ([include/thx/result.h](include/thx/result.h)) — no exceptions in library code. `thx::Result<void, Error>` is the void specialisation. `Result<T>` exposes `valueOr(fallback)` and `map(f)`; `discoverAndLoad` is the one operation that breaks the pattern (it returns a `LoadSummary` so callers can react to partial failure).

Diagnostics flow through a pluggable `thx::ILogSink` ([include/thx/log.h](include/thx/log.h)). The default sink writes structured lines to `stderr`. Replace per-process with `thx::setLogSink(sink)`; passing `nullptr` silences logging entirely. `thx::restoreDefaultLogSink()` brings the built-in stderr sink back. `ServiceManager` and `PluginManager` emit structured records with source locations for every state change and failure.

There are no `THX_LOG` / `THX_ASSERT` macros. Source location is captured automatically via `__builtin_FILE`/`__builtin_LINE`/`__builtin_FUNCTION` defaults on GCC, Clang, and MSVC ≥ VS 2019 16.6 (`_MSC_VER 1926`). Call sites use the free functions directly:

```cpp
thx::log(thx::LogLevel::Warn, "message");
thx::assertThat(condition, "message");   // logs at Error if false; std::abort() in Debug builds only
```

`assertThat` never silently swallows its condition — it always emits the diagnostic before deciding whether to abort.

### Versioning

[thx::Version](include/thx/version_type.h) is a three-component numeric version (`major.minor.patch`) with `constexpr` comparison. It is intentionally *not* full semver — there are no pre-release or build-metadata fields. The framework may grow them back if a real consumer needs them; for now the simpler shape keeps the type trivially layout-compatible across compilers, which matters because it crosses the DSO boundary by value. `thx::THORAX_VERSION` is generated from the CMake project version into [include/thx/version.h.in](include/thx/version.h.in). `Version::pack()` packs major/minor/patch into a `uint32_t` (8/8/16 bits) for crossing the C plugin ABI; the `Version(uint32_t)` constructor is the inverse. The packed form is a deliberate wire encoding, not a property of `Version`'s in-memory layout. `Version::compatible(required, provided)` is the static method used both by `PluginHandle::open()` to gate `thx_abi_version()` and by `PluginManager` to check each `ServiceRequirement` reported by `IPlugin::required()`.

## Layout & conventions

```
include/thx/         cross-cutting public headers (thorax umbrella, Library, Registry, Result, Version, log, StringView/Span, to_string)
include/thx/service/ ServiceManager, Service<>, ServiceID, IService (+ .inl)
include/thx/plugin/  PluginManager, PluginHandle, PluginGarbage, IPlugin, plugin ABI macros (platform.h)
include/thx/rtti/    public compile-time helpers (TypeName)
include/thx/detail/  private implementation helpers (hash) — installed alongside the public headers because transitively included, but not part of the user-facing surface
src/                 cross-cutting .cpp (thorax, log, library, registry)
src/service/         service-layer .cpp
src/plugin/          plugin-layer .cpp
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

**Adding a new in-tree plugin:** create `plugins/<name>/{CMakeLists.txt,src,include/thx/plugins/<name>}`, add `add_subdirectory(<name>)` to [plugins/CMakeLists.txt](plugins/CMakeLists.txt), call `thx_set_warnings(m_plugin<name>)` and add `-fvisibility=hidden` on non-MSVC inside its CMakeLists, then add `install(TARGETS m_plugin<name> …)` plus the `install(DIRECTORY plugins/<name>/include/ …)` block in the `THORAX_INSTALL` section of the root CMakeLists. The plugin's interface header must live under `plugins/<name>/include/thx/plugins/<name>/` so consumers include it as `<thx/plugins/<name>/<name>_service.h>` after install.

## Work tracking

Concrete follow-up items — known issues, hardening tasks, doc gaps — are tracked in [WORK.md](WORK.md). Add to it as new issues are spotted; clear items as they ship.
